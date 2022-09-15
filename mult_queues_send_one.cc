/*
*
*       one queue multiple tx and one rx read
*       queues type: direct
*       only one scheduler domain
*
*       author: jseroczy(serek90)
*/


#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <error.h>
#include <pthread.h>
#include <getopt.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include "dlb.h"
#include "ats_dlb_queue.h"

#define TX_TH_NUM 4
#define RETRY_LIMIT 1000000000

typedef struct {
    dlb_port_hdl_t port;
    int queue_id;
    int efd;
    int th_num;
} thread_args_t;

enum wait_mode_t {
    POLL,
    INTERRUPT,
} wait_mode = INTERRUPT;

DLB_device dlb_dev;
dlb_port_hdl_t rx_port_g; //JSJS delete
static void *rx_traffic(void *__args)
{
    thread_args_t *args = (thread_args_t *) __args;
    dlb_event_t events[40];
    printf("Hi from rx thread\n");

    int num = 0;
    int ret = 0;
    dlb_queue_depth_levels_t lvl;
    for(;;)
    {
	ret = dlb_recv(args->port,
                           40,
                           (wait_mode == POLL),
                           &events[0]);
        if (ret == -1)
        {
            printf("Problem with reciving events");
            break;
        }

	printf("Received:  %d \t cap: %d\n", ret, dlb_adv_read_queue_depth_counter(dlb_dev.get_domain(), args->queue_id, true, lvl));
        printf("Printing received events\n");
        for(int i = 0; i < ret; i++)
        {
            printf("\tthread num: %d\t", events[i].adv_send.udata16);
            printf("data: %ld\n\n", events[i].adv_send.udata64);
        }
	sleep(7);

    }

    return NULL;
}

static void *tx_traffic(void *__args)
{
    thread_args_t *args = (thread_args_t *) __args;
    dlb_event_t events[1];
    static int cntr = 0;

    printf("Hi from tx thread\n");

    /* Initialize the static fields in the send events */
    for (int i = 0; i < 1; i++) {
        events[i].send.flow_id = 0;
        events[i].send.queue_id = args->queue_id;
        events[i].send.sched_type = SCHED_DIRECTED;
        events[i].send.priority = 0;
    }

    /* Send the events */
    int ret = 0;
    int num = 0;
    dlb_queue_depth_levels_t lvl;
    for(;;)
    {
        events[0].adv_send.udata64 = cntr++;
        events[0].adv_send.udata16 = args->th_num;

        ret = dlb_send(args->port, 1, &events[0]);

	if (ret == -1)
        {
            printf("thread %d problem with packet port = %p\n", args->th_num, args->port);
            break;
        }
	printf("%d \t", cntr);
        printf("Thread: %d Send %d \t cap: %d\n", args->th_num, ret, dlb_adv_read_queue_depth_counter(dlb_dev.get_domain(), args->queue_id, true, lvl));
	sleep(1);
    }

    return NULL;
}

int main(int argc, char **argv)
{
	pthread_t rx_thread, tx_thread[TX_TH_NUM];
	thread_args_t rx_args, tx_args[TX_TH_NUM];
	int rx_port_id, tx_port_id[TX_TH_NUM];

	/* queque creation */
	printf("Create queque\n");
	DLB_queue dlb_queue(4, dlb_dev.get_domain(), dlb_dev.get_ldb_pool_id(), dlb_dev.get_dir_pool_id(), dlb_dev.get_cap());
	rx_args.queue_id = dlb_queue.get_queue_id();

	for(int i = 0; i < TX_TH_NUM; i++)
 		tx_args[i].queue_id = rx_args.queue_id;

	/* TX ports creation */
	printf("Port tx creation\n");
	for(int i = 0; i < TX_TH_NUM; i++)
		tx_args[i].port = dlb_queue.add_port();

	/* rx port creation */
	rx_port_g = rx_args.port = dlb_queue.add_port();

	dlb_dev.start_sched();

/************************************************
* Thread preparation
************************************************/
	pthread_create(&rx_thread, NULL, rx_traffic, &rx_args);
    	/* Add sleep here to make sure the rx_thread is staretd before tx_thread */
    	usleep(1000);

	/* thread creation */
    	printf("Creating tx and rx threads\n");
    	for(int i = 0; i < TX_TH_NUM; i++)
   	{
        	tx_args[i].th_num = i;
    		pthread_create(&tx_thread[i], NULL, tx_traffic, &tx_args[i]);
    	}

    	/* Wait for threads to complete */
   	for(int i = 0; i < TX_TH_NUM; i++)
    		pthread_join(tx_thread[i], NULL);
    	pthread_join(rx_thread, NULL);


/********************************************
* Clean dlb device
*********************************************/

    if (dlb_detach_port(rx_args.port) == -1)
        error(1, errno, "dlb_detach_port");

    for(int i = 0; i < TX_TH_NUM; i++)
    {
        if (dlb_detach_port(tx_args[i].port) == -1)
            error(1, errno, "dlb_detach_port");
    }

    return 0;
}
