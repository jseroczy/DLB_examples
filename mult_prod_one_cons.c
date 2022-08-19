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

typedef struct
{
	dlb_resources_t rsrcs;
	dlb_dev_cap_t cap;
	int dev_id;
	dlb_hdl_t handl;
} dlb_dev;

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

static int print_resources(
    dlb_resources_t *rsrcs, dlb_dev_cap_t *cap)
{
    printf("DLB's available resources:\n");
    printf("\tDomains:           %d\n", rsrcs->num_sched_domains);
    printf("\tLDB queues:        %d\n", rsrcs->num_ldb_queues);
    printf("\tLDB ports:         %d\n", rsrcs->num_ldb_ports);
    printf("\tDIR ports:         %d\n", rsrcs->num_dir_ports);
    printf("\tSN slots:          %d,%d\n", rsrcs->num_sn_slots[0],
           rsrcs->num_sn_slots[1]);
    printf("\tES entries:        %d\n", rsrcs->num_ldb_event_state_entries);
    printf("\tContig ES entries: %d\n",
           rsrcs->max_contiguous_ldb_event_state_entries);
    if (!cap->combined_credits) {
        printf("\tLDB credits:       %d\n", rsrcs->num_ldb_credits);
        printf("\tContig LDB cred:   %d\n", rsrcs->max_contiguous_ldb_credits);
        printf("\tDIR credits:       %d\n", rsrcs->num_dir_credits);
        printf("\tContig DIR cred:   %d\n", rsrcs->max_contiguous_dir_credits);
        printf("\tLDB credit pls:    %d\n", rsrcs->num_ldb_credit_pools);
        printf("\tDIR credit pls:    %d\n", rsrcs->num_dir_credit_pools);
    } else {
        printf("\tCredits:           %d\n", rsrcs->num_credits);
        printf("\tCredit pools:      %d\n", rsrcs->num_credit_pools);
    }
    printf("\n");

    return 0;
}

static int create_dir_queue(
    dlb_domain_hdl_t domain,
    int port_id)
{
    return dlb_create_dir_queue(domain, port_id);
}

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

static int create_dir_port(
    dlb_domain_hdl_t domain,
    int ldb_pool,
    int dir_pool,
    int queue_id,
    dlb_dev dlb_d)
{
    dlb_create_port_t args;

    if (!dlb_d.cap.combined_credits) {
        args.ldb_credit_pool_id = ldb_pool;
        args.dir_credit_pool_id = dir_pool;
    } else {
        args.credit_pool_id = ldb_pool;
    }

    args.cq_depth = CQ_DEPTH;

    return dlb_create_dir_port(domain, &args, queue_id);
}

static int create_sched_domain(
    dlb_dev dlb)
{
    int p_rsrsc = partial_resources;
    dlb_create_sched_domain_t args;

    args.num_ldb_queues = 0;
    args.num_ldb_ports = 0;
    args.num_dir_ports = TX_TH_NUM + 1;
    args.num_ldb_event_state_entries = TX_TH_NUM;
    if (!dlb.cap.combined_credits) {
        args.num_ldb_credits = dlb.rsrcs.max_contiguous_ldb_credits * p_rsrsc / 100;
        args.num_dir_credits = dlb.rsrcs.max_contiguous_dir_credits * p_rsrsc / 100;
        args.num_ldb_credit_pools = 1;
        args.num_dir_credit_pools = 1;
    } else {
        args.num_credits = dlb.rsrcs.num_credits * p_rsrsc / 100;
        args.num_credit_pools = 1;
    }
    args.num_sn_slots[0] = 0;
    args.num_sn_slots[1] = 0;

    return dlb_create_sched_domain(dlb.handl, &args);
}


int main(int argc, char **argv)
{
    dlb_dev dlb_d;
    pthread_t rx_thread, tx_thread[TX_TH_NUM];
    thread_args_t rx_args, tx_args[TX_TH_NUM];
    dlb_domain_hdl_t domain;
    int domain_id;
    int rx_port_id, tx_port_id[TX_TH_NUM];

    /* dlb device create */
    if (dlb_open(dlb_d.dev_id, &dlb_d.handl) == -1)
        error(1, errno, "dlb_open");

    if (dlb_get_dev_capabilities(dlb_d.handl, &dlb_d.cap))
        error(1, errno, "dlb_get_dev_capabilities");

    if (dlb_get_num_resources(dlb_d.handl, &dlb_d.rsrcs))
        error(1, errno, "dlb_get_num_resources");

    if (print_resources(&dlb_d.rsrcs, &dlb_d.cap))
        error(1, errno, "print_resources");

    /* Create sheduler domain */
    domain_id = create_sched_domain(dlb_d);
    if (domain_id == -1)
        error(1, errno, "dlb_create_sched_domain");

    domain = dlb_attach_sched_domain(dlb_d.handl, domain_id);
    if (domain == NULL)
        error(1, errno, "dlb_attach_sched_domain");
////////////////////////////////////////////////////////////////////////////////////
        int max_ldb_credits = dlb_d.rsrcs.num_ldb_credits * partial_resources / 100;
        int max_dir_credits = dlb_d.rsrcs.num_dir_credits * partial_resources / 100;

        if (use_max_credit_ldb == true)
            ldb_pool_id = dlb_create_ldb_credit_pool(domain, max_ldb_credits);
        else
            if (num_credit_ldb <= max_ldb_credits)
                ldb_pool_id = dlb_create_ldb_credit_pool(domain,
                                                         num_credit_ldb);
            else
                error(1, EINVAL, "Requested ldb credits are unavailable!");

        if (ldb_pool_id == -1)
            error(1, errno, "dlb_create_ldb_credit_pool");

        if (use_max_credit_dir == true)
            dir_pool_id = dlb_create_dir_credit_pool(domain, max_dir_credits);
        else
            if (num_credit_dir <= max_dir_credits)
                dir_pool_id = dlb_create_dir_credit_pool(domain,
                                                         num_credit_dir);
            else
                error(1, EINVAL, "Requested dir credits are unavailable!");

        if (dir_pool_id == -1)
            error(1, errno, "dlb_create_dir_credit_pool");
/////////////////////////////////////////////////////////////////////////////////////////////
    /* queque creation */
    printf("Create queque\n");
    rx_args.queue_id = create_dir_queue(domain, -1);
    if (rx_args.queue_id == -1)
        error(1, errno, "dlb_create_dir_queue");

    for(int i = 0; i < TX_TH_NUM; i++)
        tx_args[i].queue_id = rx_args.queue_id;


    /* tx port creation */
    printf("Port tx creation\n");
    for(int i = 0; i < TX_TH_NUM; i++)
    {
        tx_port_id[i] = create_dir_port(domain, ldb_pool_id, dir_pool_id, -1, dlb_d);
        if (tx_port_id[i] == -1)
	      {
            printf("tx port_id: %d\n", i);
            error(1, errno, "dlb_create_dir_port");
	      }

        tx_args[i].port = dlb_attach_dir_port(domain, tx_port_id[i]);
        if (tx_args[i].port == NULL)
            error(1, errno, "dlb_attach_dir_port");
    }
    /* rx port creation */
    rx_port_id = create_dir_port(domain, ldb_pool_id, dir_pool_id, rx_args.queue_id, dlb_d);
    if (rx_port_id == -1)
        error(1, errno, "dlb_create_dir_port");

    rx_args.port = dlb_attach_dir_port(domain, rx_port_id);
    if (rx_args.port == NULL)
        error(1, errno, "dlb_attach_dir_port");

    if (dlb_launch_domain_alert_thread(domain, NULL, NULL))
        error(1, errno, "dlb_launch_domain_alert_thread");
    /* start scheduler */
    if (dlb_start_sched_domain(domain))
        error(1, errno, "dlb_start_sched_domain");

/************************************************
* Thread preparation
************************************************/

    /* Add sleep here to make sure the rx_thread is staretd before tx_thread */
    pthread_create(&rx_thread, NULL, rx_traffic, &rx_args);
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

    /* dlb domain shced clean */
    if (dlb_detach_sched_domain(domain) == -1)
        error(1, errno, "dlb_detach_sched_domain");

    if (dlb_reset_sched_domain(dlb_d.handl, domain_id) == -1)
        error(1, errno, "dlb_reset_sched_domain");

    /* dlb device clean */
    if (dlb_close(dlb_d.handl) == -1)
        error(1, errno, "dlb_close");

    return 0;
}
