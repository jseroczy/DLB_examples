
/*
*
* author: jseroczy
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

#define TX_TH_NUM 40
#define NUM_EVENTS_PER_LOOP 4000
#define RETRY_LIMIT 1000000000
#define CQ_DEPTH 128

static int partial_resources = 100;
static bool use_max_credit_ldb = true;
static bool use_max_credit_dir = true;
int ldb_pool_id, dir_pool_id;
static int num_credit_dir;
static int num_credit_ldb;
static void *rx_traffic(void *__args)
{
    thread_args_t *args = (thread_args_t *) __args;
    dlb_event_t events[NUM_EVENTS_PER_LOOP * TX_TH_NUM];
    printf("Hi from rx thread\n");

    int num = 0;
    int ret = 0;
    /* Receive the events */
    for (int j = 0; num != (NUM_EVENTS_PER_LOOP * TX_TH_NUM) && j < RETRY_LIMIT; j++)
    {
        ret = dlb_recv(args->port,
                           NUM_EVENTS_PER_LOOP * TX_TH_NUM -num,
                           (wait_mode == INTERRUPT),
                           &events[num]);
        if (ret == -1)
	      {
            printf("Problem with reciving events");
            break;
        }
        printf("Received: %d\n", num);

        num += ret;
            if ((j != 0) && (j % 10000000 == 0))
                printf("[%s()] Rx blocked for %d iterations\n", __func__, j);

        if (num != NUM_EVENTS_PER_LOOP)
        {
            printf("[%s()] Recv'ed %d events (iter %d)!\n", __func__, num, j);
         //   exit(-1);
        }

        printf("num : %d", num);
        printf("\t %ld", events[num].adv_send.udata64);
        printf("\t %d", events[num].adv_send.udata16);

    }

    printf("[%s()] Received %d events\n",
           __func__,  num);

    /* print received events */
    sleep(1);
    printf("Printing received events\n");
    for(int i = 0; i < NUM_EVENTS_PER_LOOP * TX_TH_NUM; i++)
    {
      printf("thread num: %d\t", events[i].adv_send.udata16);
      printf("data: %ld\n", events[i].adv_send.udata64);
    }
    return NULL;
}

static void *tx_traffic(void *__args)
{
    thread_args_t *args = (thread_args_t *) __args;
    dlb_event_t events[NUM_EVENTS_PER_LOOP];

    printf("Hi from tx thread\n");

    /* Initialize the static fields in the send events */
    for (int i = 0; i < NUM_EVENTS_PER_LOOP; i++) {
        events[i].send.flow_id = 0;
        events[i].send.queue_id = args->queue_id;
        events[i].send.sched_type = SCHED_DIRECTED;
        events[i].send.priority = 0;
    }

    /* Initialize the dynamic fields in the send events */
    for (int i = 0; i < NUM_EVENTS_PER_LOOP; i++)
    {
         events[i].adv_send.udata64 = i;
         events[i].adv_send.udata16 = args->th_num;
    }

    /* Send the events */
    int ret = 0;
    int num = 0;
    for (int j = 0; num != NUM_EVENTS_PER_LOOP && j < RETRY_LIMIT; j++)
    {
        ret = dlb_send(args->port, NUM_EVENTS_PER_LOOP-num, &events[num]);
        if (ret == -1)
        {
	          printf("thread %d problem with packet j = %d\t port = %d\n", args->th_num, j, args->port);
            break;
	      }

        num += ret;
        if ((j != 0) && (j % 10000000 == 0))
                printf("[%s()] Tx blocked for %d iterations\n", __func__, j);
    }


    printf("[%s()] Sent %d events\n",
           __func__, NUM_EVENTS_PER_LOOP);

    return NULL;
}

/*******************************************
* Threads
* One producer and many consumers
*******************************************/
pthread_t rx_thread, tx_thread[TX_TH_NUM];
thread_args_t rx_args, tx_args[TX_TH_NUM];

/******************************************
*main function
*******************************************/
int main(int argc, char **argv)
{
    dlb_dev dlb_d;
    int rx_port_id, tx_port_id[TX_TH_NUM];

    return 0;
}
