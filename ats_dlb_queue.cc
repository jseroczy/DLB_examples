#include "ats_dlb_queue.h"
#include "stdio.h"
#include <error.h>

DLB_queue::DLB_queue(int num_of_ports, dlb_domain_hdl_t _domain, int _ldb_pool_id, int _dir_pool_id, dlb_dev_cap_t _cap) : domain(_domain), ldb_pool_id(_ldb_pool_id), dir_pool_id(_dir_pool_id)
{
	cap = _cap;
	queue_id = dlb_create_dir_queue(domain, -1);
	if (queue_id == -1)
		error(1, errno, "dlb_create_dir_queue");
}

DLB_queue::~DLB_queue()
{
	/* Close all ports */
	/* Detach all ports */
    //if (dlb_detach_port(tx_args.port) == -1)
        //error(1, errno, "dlb_detach_port");
}

dlb_port_hdl_t DLB_queue::add_port()
{
	printf("Adding new port to queue\n");

	/* Prepare args for the port */
	dlb_create_port_t args;

	if (!cap.combined_credits) {
		args.ldb_credit_pool_id = ldb_pool_id;
		args.dir_credit_pool_id = dir_pool_id;
	} else {
		args.credit_pool_id = ldb_pool_id;
	}

	args.cq_depth = CQ_DEPTH;

	/* Create port */
	int port_id = dlb_create_dir_port(domain, &args, queue_id);
	if (port_id == -1)
		error(1, errno, "dlb_create_dir_port");

	dlb_port_hdl_t port = dlb_attach_dir_port(domain, port_id);
	if (port == NULL)
		error(1, errno, "dlb_attach_dir_port");

	return port;
}

void DLB_queue::print_ports()
{
	printf("Printing all ports which this queue contains:\n");
}

DLB_device::DLB_device()
{
	printf("DEBUG: Hi I am DLB_device constructor\n");

	/* Open DLB device */
	if (dlb_open(ID, &dlb_hdl) == -1)
		error(1, errno, "dlb_open");

	/* get DLB device capabilietes */
	if (dlb_get_dev_capabilities(dlb_hdl, &cap))
		error(1, errno, "dlb_get_dev_capabilities");

	/* Get DLB device resources information */
	if (dlb_get_num_resources(dlb_hdl, &rsrcs))
		error(1, errno, "dlb_get_num_resources");

	/* Creat scheduler domain */
	domain_id = create_sched_domain();
	if (domain_id == -1)
		error(1, errno, "dlb_create_sched_domain");

	domain = dlb_attach_sched_domain(dlb_hdl, domain_id);
	if (domain == NULL)
		error(1, errno, "dlb_attach_sched_domain");

	if (!cap.combined_credits)
	{
		int max_ldb_credits = rsrcs.num_ldb_credits * partial_resources / 100;
		int max_dir_credits = rsrcs.num_dir_credits * partial_resources / 100;

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
	}else{
		int max_credits = rsrcs.num_credits * partial_resources / 100;

		if (use_max_credit_combined == true)
			ldb_pool_id = dlb_create_credit_pool(domain, max_credits);
		else
			if (num_credit_combined <= max_credits)
				ldb_pool_id = dlb_create_credit_pool(domain,
                                                    	num_credit_combined);
		else
			error(1, EINVAL, "Requested combined credits are unavailable!");

		if (ldb_pool_id == -1)
			error(1, errno, "dlb_create_credit_pool");
	}

	printf("Succesfully create DLB device class object\n");
}


DLB_device::~DLB_device()
{
        printf("DEBUG: Hi I am DLB_device destructor\n");

	/* Detach and reset shceduler domain */
	if (dlb_detach_sched_domain(domain) == -1)
		error(1, errno, "dlb_detach_sched_domain");

	if (dlb_reset_sched_domain(dlb_hdl, domain_id) == -1)
		error(1, errno, "dlb_reset_sched_domain");

	/* Close the DLB device */
	if (dlb_close(dlb_hdl) == -1)
		error(1, errno, "dlb_close");

}

void DLB_device::start_sched()
{
	if (dlb_launch_domain_alert_thread(domain, NULL, NULL))
		error(1, errno, "dlb_launch_domain_alert_thread");

	if (dlb_start_sched_domain(domain))
		error(1, errno, "dlb_start_sched_domain");
}


int DLB_device::create_sched_domain()
{
    int p_rsrsc = partial_resources;
    dlb_create_sched_domain_t args;

    args.num_ldb_queues = 0;
    args.num_ldb_ports = 0;
    args.num_dir_ports = 64; // for now all 64 ports, change it later
    args.num_ldb_event_state_entries = 0;
    if (!cap.combined_credits) {
        args.num_ldb_credits = rsrcs.max_contiguous_ldb_credits * p_rsrsc / 100;
        args.num_dir_credits = rsrcs.max_contiguous_dir_credits * p_rsrsc / 100;
        args.num_ldb_credit_pools = 1;
        args.num_dir_credit_pools = 1;
    } else {
        args.num_credits = rsrcs.num_credits * p_rsrsc / 100;
        args.num_credit_pools = 1;
    }
    args.num_sn_slots[0] = 0;
    args.num_sn_slots[1] = 0;

    return dlb_create_sched_domain(dlb_hdl, &args);
}

void DLB_device::print_resources()
{
	printf("DLB's available resources:\n");
	printf("\tDomains:           %d\n", rsrcs.num_sched_domains);
	printf("\tLDB queues:        %d\n", rsrcs.num_ldb_queues);
	printf("\tLDB ports:         %d\n", rsrcs.num_ldb_ports);
	printf("\tDIR ports:         %d\n", rsrcs.num_dir_ports);
	printf("\tSN slots:          %d,%d\n", rsrcs.num_sn_slots[0],
		rsrcs.num_sn_slots[1]);
	printf("\tES entries:        %d\n", rsrcs.num_ldb_event_state_entries);
	printf("\tContig ES entries: %d\n",
		rsrcs.max_contiguous_ldb_event_state_entries);
	if (!cap.combined_credits) {
		printf("\tLDB credits:       %d\n", rsrcs.num_ldb_credits);
		printf("\tContig LDB cred:   %d\n", rsrcs.max_contiguous_ldb_credits);
		printf("\tDIR credits:       %d\n", rsrcs.num_dir_credits);
		printf("\tContig DIR cred:   %d\n", rsrcs.max_contiguous_dir_credits);
		printf("\tLDB credit pls:    %d\n", rsrcs.num_ldb_credit_pools);
		printf("\tDIR credit pls:    %d\n", rsrcs.num_dir_credit_pools);
	} else {
		printf("\tCredits:           %d\n", rsrcs.num_credits);
		printf("\tCredit pools:      %d\n", rsrcs.num_credit_pools);
	}
	printf("\n");
}
