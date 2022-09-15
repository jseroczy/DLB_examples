#ifndef DLB_ATS_H
#define DLB_ATS_H

#include "dlb.h"
#include <vector>

#define CQ_DEPTH 128

class DLB_queue
{
	int queue_id;
	dlb_domain_hdl_t domain;
	/* pool ids */
        int ldb_pool_id, dir_pool_id;
	dlb_dev_cap_t cap;

	std::vector<dlb_port_hdl_t> ports;

public:
	int get_queue_id() { return queue_id; }
	void print_ports();
	dlb_port_hdl_t add_port();
	DLB_queue(int, dlb_domain_hdl_t, int, int, dlb_dev_cap_t);
	~DLB_queue();
};

class DLB_device
{
	dlb_dev_cap_t cap;
	dlb_hdl_t dlb_hdl;
	int ID;
	dlb_resources_t rsrcs;

	/* scheduler domain */
	dlb_domain_hdl_t domain;
	int domain_id;

	/* pool ids */
	int ldb_pool_id, dir_pool_id;

	std::vector< class DLB_queue> queues;

	int partial_resources = 100;
	int num_credit_combined;
	int num_credit_ldb;
	int num_credit_dir;
	bool use_max_credit_combined = true;
	bool use_max_credit_ldb = true;
	bool use_max_credit_dir = true;

	/* methods */
	int create_sched_domain();

public:
	/*getters */
	int get_ldb_pool_id() { return ldb_pool_id; }
	int get_dir_pool_id() { return dir_pool_id; }
	dlb_domain_hdl_t get_domain() { return domain; }
	dlb_dev_cap_t get_cap() { return cap; }

	void print_resources();
	void start_sched();
	DLB_device();
	~DLB_device();
};


#endif
