/*
 * Copyright (c) 2008 Princeton University
 * Copyright (c) 2016 Georgia Institute of Technology
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


#include "mem/ruby/network/garnet/RoutingUnit.hh"
#include <algorithm>
#include <iostream>
#include <fstream>
#include <unistd.h>
#include "base/cast.hh"
#include "debug/RubyNetwork.hh"
#include "mem/ruby/network/garnet/InputUnit.hh"
#include "mem/ruby/network/garnet/OutputUnit.hh"
#include "mem/ruby/network/garnet/Router.hh"
#include "mem/ruby/slicc_interface/Message.hh"
#include "mem/ruby/network/garnet/flit.hh"
#include "cpu/kvm/base.hh"
#include <Python.h>
#include "pyhelper.hpp"
#include <unistd.h>
#include <math.h>

RoutingUnit::RoutingUnit(Router *router)
{
    m_router = router;
	//std::cout<<"ID is "<<m_router->get_id()<<std::endl;
    m_routing_table.clear();
    m_weight_table.clear();
}

void
RoutingUnit::addRoute(std::vector<NetDest>& routing_table_entry)
{
    if (routing_table_entry.size() > m_routing_table.size()) {
        m_routing_table.resize(routing_table_entry.size());
    }
    for (int v = 0; v < routing_table_entry.size(); v++) {
        m_routing_table[v].push_back(routing_table_entry[v]);
    }
}

void
RoutingUnit::addWeight(int link_weight)
{
    m_weight_table.push_back(link_weight);
}

bool
RoutingUnit::supportsVnet(int vnet, std::vector<int> sVnets)
{
    // If all vnets are supported, return true
    if (sVnets.size() == 0) {
        return true;
    }

    // Find the vnet in the vector, return true
    if (std::find(sVnets.begin(), sVnets.end(), vnet) != sVnets.end()) {
        return true;
    }

    // Not supported vnet
    return false;
}

/*
 * This is the default routing algorithm in garnet.
 * The routing table is populated during topology creation.
 * Routes can be biased via weight assignments in the topology file.
 * Correct weight assignments are critical to provide deadlock avoidance.
 */
int
RoutingUnit::lookupRoutingTable(int vnet, NetDest msg_destination)
{
    // First find all possible output link candidates
    // For ordered vnet, just choose the first
    // (to make sure different packets don't choose different routes)
    // For unordered vnet, randomly choose any of the links
    // To have a strict ordering between links, they should be given
    // different weights in the topology file

    int output_link = -1;
    int min_weight = INFINITE_;
    std::vector<int> output_link_candidates;
    int num_candidates = 0;

    // Identify the minimum weight among the candidate output links
    for (int link = 0; link < m_routing_table[vnet].size(); link++) {
        if (msg_destination.intersectionIsNotEmpty(
            m_routing_table[vnet][link])) {

        if (m_weight_table[link] <= min_weight)
            min_weight = m_weight_table[link];
        }
    }

    // Collect all candidate output links with this minimum weight
    for (int link = 0; link < m_routing_table[vnet].size(); link++) {
        if (msg_destination.intersectionIsNotEmpty(
            m_routing_table[vnet][link])) {

            if (m_weight_table[link] == min_weight) {
                num_candidates++;
                output_link_candidates.push_back(link);
            }
        }
    }

    if (output_link_candidates.size() == 0) {
        fatal("Fatal Error:: No Route exists from this Router.");
        exit(0);
    }

    // Randomly select any candidate output link
    int candidate = 0;
    if (!(m_router->get_net_ptr())->isVNetOrdered(vnet))
        candidate = rand() % num_candidates;

    output_link = output_link_candidates.at(candidate);
    return output_link;
}


void
RoutingUnit::addInDirection(PortDirection inport_dirn, int inport_idx)
{
    m_inports_dirn2idx[inport_dirn] = inport_idx;
    m_inports_idx2dirn[inport_idx]  = inport_dirn;
}

void
RoutingUnit::addOutDirection(PortDirection outport_dirn, int outport_idx)
{
    m_outports_dirn2idx[outport_dirn] = outport_idx;
    m_outports_idx2dirn[outport_idx]  = outport_dirn;
}

// outportCompute() is called by the InputUnit
// It calls the routing table by default.
// A template for adaptive topology-specific routing algorithm
// implementations using port directions rather than a static routing
// table is provided here.

int
RoutingUnit::outportCompute(flit *t_flit, int inport,
                            PortDirection inport_dirn)
{
    int outport = -1;
	RouteInfo route = t_flit->get_route();
	RoutingAlgorithm routing_algorithm = (RoutingAlgorithm) m_router->get_net_ptr()->getRoutingAlgorithm();

    if (route.dest_router == m_router->get_id() && routing_algorithm != 7 && routing_algorithm !=4 && routing_algorithm != 9 && routing_algorithm != 10) {

        // Multiple NIs may be connected to this router,
        // all with output port direction = "Local"
        // Get exact outport id from table
       outport = lookupRoutingTable(route.vnet, route.net_dest);
       return outport;
    }

    // Routing Algorithm set in GarnetNetwork.py
    // Can be over-ridden from command line using --routing-algorithm = 1

    switch (routing_algorithm) {
        case TABLE_:
			outport = lookupRoutingTable(route.vnet, route.net_dest);
			 break;
        case 2:
			outport = outportComputeXY(route, inport, inport_dirn);
			 break;
        case 3:
			 outport = outportComputeOE(route, inport, inport_dirn);
			 break;
		case 4:
			 outport = outportComputeQ_Routing(t_flit, inport, inport_dirn);
			 break;
        case 5:
			 outport = outportComputeQ_RoutingTesting(t_flit, inport, inport_dirn);
			 break;
		case 6:
			 outport = outportComputeQ_RoutingPythonTesting(t_flit, inport, inport_dirn);
			 break;
		case 7:
			 outport = outportComputeDQNPython(t_flit, inport, inport_dirn);
			 break;
		case 8:
			 outport = outportComputeDQNPythonTesting(t_flit, inport, inport_dirn);
			 break;
		case 9:
			 outport = outportComputeDQNvcPython(t_flit, inport, inport_dirn);
			 break;
		case 10:
			 outport = outportComputeDQNhopsPython(t_flit, inport, inport_dirn);
			 break;
		case 11:
			 outport = outportComputeDQNvcPythonTesting(t_flit, inport, inport_dirn);
			 break;
		case 12:
			 outport = outportComputeDQNhopsPythonTesting(t_flit, inport, inport_dirn);
			 break;
        default:
			 outport = lookupRoutingTable(route.vnet, route.net_dest);
			 break;
    }

    assert(outport != -1);
    return outport;
}

// XY routing implemented using port directions
// Only for reference purpose in a Mesh
// By default Garnet uses the routing table
int
RoutingUnit::outportComputeXY(RouteInfo route,
                              int inport,
                              PortDirection inport_dirn)
{
    PortDirection outport_dirn = "Unknown";

    int M5_VAR_USED num_rows = m_router->get_net_ptr()->getNumRows();
    int num_cols = m_router->get_net_ptr()->getNumCols();
    assert(num_rows > 0 && num_cols > 0);

    int my_id = m_router->get_id();
    int my_x = my_id % num_cols;
    int my_y = my_id / num_cols;

    int dest_id = route.dest_router;
    int dest_x = dest_id % num_cols;
    int dest_y = dest_id / num_cols;

    int x_hops = abs(dest_x - my_x);
    int y_hops = abs(dest_y - my_y);

    bool x_dirn = (dest_x >= my_x);
    bool y_dirn = (dest_y >= my_y);
    // already checked that in outportCompute() function
    assert(!(x_hops == 0 && y_hops == 0));
	/*if(OutputUnit::has_free_vc(route.vnet)) {
		std::cout<<"Output function called\n";
	}*/
	auto outputunit = m_router->getOutputUnit(m_outports_dirn2idx["East"]);
	if(outputunit->has_free_vc(route.vnet)) {
		//std::cout<<"Output Unit called\n";
	}
	//std::cout<<inport_dirn<<std::endl;
    if (x_hops > 0) {
        if (x_dirn) {
            assert(inport_dirn == "Local" || inport_dirn == "West");
            outport_dirn = "East";
        } else {
            assert(inport_dirn == "Local" || inport_dirn == "East");
            outport_dirn = "West";
        }
    } else if (y_hops > 0) {
        if (y_dirn) {
            // "Local" or "South" or "West" or "East"
            assert(inport_dirn != "North");
            outport_dirn = "North";
        } else {
            // "Local" or "North" or "West" or "East"
            assert(inport_dirn != "South");
            outport_dirn = "South";
        }
    } else {
        // x_hops == 0 and y_hops == 0
        // this is not possible
        // already checked that in outportCompute() function
        panic("x_hops == y_hops == 0");
    }
	std::ofstream file;
	file.open("/home/b170330cs/NoC/gem5/XY_path.txt",std::ios_base::app);

	
	file <<"Current router id: "<<my_id<<"\tInport direction: "<<inport_dirn<<"\tOutport Dirn: "<<outport_dirn<<"\n";
    file.close();
	return m_outports_dirn2idx[outport_dirn];
}

//Odd Even routing algorithm

int
RoutingUnit::outportComputeOE(RouteInfo route,
                              int inport,
                              PortDirection inport_dirn)
{
    PortDirection outport_dirn = "Unknown";
    
    int M5_VAR_USED num_rows = m_router->get_net_ptr()->getNumRows();
    int num_cols = m_router->get_net_ptr()->getNumCols();
    assert(num_rows > 0 && num_cols > 0);

    int my_id = m_router->get_id();
    int my_x = my_id % num_cols;
    int my_y = my_id / num_cols;

    int dest_id = route.dest_router;
    int dest_x = dest_id % num_cols;
    int dest_y = dest_id / num_cols;

    int src_id = route.src_router;
    int src_x = src_id % num_cols;
    int src_y = src_id / num_cols;

    int x_hops = dest_x - my_x;
    int y_hops = dest_y - my_y;

    // already checked that in outportCompute() function
    assert(!(x_hops == 0 && y_hops == 0));
	
	if(x_hops == 0) {
		if(y_hops < 0) {
			outport_dirn = "South";
		}
		else {
			outport_dirn = "North";
		}
	}
	else {
		if(x_hops > 0) {
			if(y_hops == 0) {
				outport_dirn = "East";
			}
			else {
				if(my_x % 2 != 0 || my_x == src_x) {
					if(y_hops < 0) {
						outport_dirn = "South";
					}
					else {
						outport_dirn = "North";
					}
				}
				if(dest_x % 2 != 0 || x_hops != 1) {
					outport_dirn = "East";
				}
			}
		}
		else {
			outport_dirn = "West";
			if(y_hops != 0) {
				if(my_x % 2 == 0) {
					if(y_hops < 0) {
						outport_dirn = "South";
					}
					else {
						outport_dirn = "North";
					}
				}
			}	
		}
	}
    return m_outports_dirn2idx[outport_dirn];
}

#define EPSILON 0.3
#define epsilon 0.3
#define NROUTERS 64
#define NACTIONS 4
#define GRIDSIZE 8
#define LEARNINGRATE 0.09
#define DISCOUNTRATE 0.7
#define EPSILON_DECAY 0.99975
#define MIN_EPSILON 0.001

int RoutingUnit::epsilon_greedy(std::vector<std::vector<std::vector<double>>> Q, int state, int destination) {
	float p = (float) rand() / RAND_MAX;
	if(p > EPSILON) {
		int optimalAction = std::distance(Q[state][destination].begin(), std::min_element(Q[state][destination].begin(), Q[state][destination].end()));
		//std::cout<<"Optimal Action: "<<optimalAction<<std::endl;
		return optimalAction;
	}

        int randomAction = rand() % 4;
//	randomAction = temp % 4;
	//std::cout<<"Rchd RandomAction: "<<randomAction<<std::endl;
	return randomAction;
}


int RoutingUnit::outportComputeQ_Routing(flit *t_flit, int inport, PortDirection inport_dirn) {
	Tick src_queueing_delay = t_flit->get_src_delay();
    Tick dest_queueing_delay = (curTick() - t_flit->get_dequeue_time());
    Tick queueing_delay = src_queueing_delay + dest_queueing_delay;
//	std::cout<<"CURRENT TICK "<<curTick()<<"\n";	
	static int SEED = 0;
	if(SEED == 0){
		srand(time(NULL));
//		std::cout << "Reaching srand()" << std::endl;
		SEED += 1;
	}
	static int iter = 0;
	//std::cout << "Number of iterations: " << iter << std::endl;
	iter++;
	
    PortDirection outport_dirn = "Unknown";
    static bool isQTableInitialized = false;
	static std::vector<std::vector<std::vector<double>>> Q(NROUTERS, std::vector<std::vector<double>>(NROUTERS, std::vector<double> (NACTIONS, INT_MAX)));
	if(!isQTableInitialized){
		isQTableInitialized = true;
		if(access("Q_Table.txt", F_OK) == 0) {
			std::ifstream f_qTable {"Q_Table.txt"};
			for(int i=0;i<NROUTERS;++i) {
				for(int j=0;j<NROUTERS;++j) {
					for(int k=0;k<NACTIONS;++k) {
						f_qTable >> Q[i][j][k];
					}
				}
			}
			//f_qTable >> EPSILON;
			std::cout<<"File exist\n";
			//for(int i=0;i<NROUTERS;++i) {
			//	for(int j=0;j<NROUTERS;++j) {
			//		for(int k=0;k<NACTIONS;++k) {
			//				std::cout<<Q[i][j][k]<<" ";
			//		}
			//		std::cout<<"\n";
			//	}
			//	std::cout<<"\n";
			//}
		}
	}

	//---Initializing Q-Table---
	RouteInfo route = t_flit->get_route();

	//---Geting source and destination router details
	int M5_VAR_USED num_rows = m_router->get_net_ptr()->getNumRows();
	int num_cols = m_router->get_net_ptr()->getNumCols();
    assert(num_rows > 0 && num_cols > 0);
    int my_id = m_router->get_id();
    int my_x = my_id % num_cols;
    int my_y = my_id / num_cols;

    int dest_id = route.dest_router;

    int src_id = route.src_router;
	
	int action = epsilon_greedy(Q, my_id, dest_id);
	int prev_router_id;

	int temp_x = 0;
	int temp_y = 0;
	if(inport_dirn == "North"){
		if(my_y < num_rows - 1) {
			temp_y = my_y + 1;
			temp_x = my_x;
		}
	}
	else if(inport_dirn == "South") {
		if(my_y > 0) {
			temp_y = my_y - 1;
			temp_x = my_x;
		}
	
	}
	else if(inport_dirn == "East") {
		if(my_x < num_cols -1) {
			temp_x = my_x + 1;
			temp_y = my_y;
		}
	}
	else if(inport_dirn == "West"){
		if(my_x > 0) {
			temp_x = my_x - 1;
			temp_y = my_y;
		}
	
	}
	else{
	}

	prev_router_id = temp_y * num_cols + temp_x;

	do{
		if(action == 0 && my_y < num_rows-1) {
			outport_dirn = "North";
		}
		else if(action == 1 && my_x < num_cols-1) {
			outport_dirn = "East";
		}
		else if(action == 2 && my_y>0) {
			outport_dirn = "South";
		}
		else if(action == 3 && my_x>0){
			outport_dirn = "West";
		}
		else{
			long long random = rand();
            action = random % 4;
		}
		
	}while(outport_dirn == "Unknown");

	
	if(my_id == src_id) {
		return m_outports_dirn2idx[outport_dirn];
	}
	int prev_action = 0;
	if(inport_dirn == "North") {
		prev_action = 2;
	}
	else if(inport_dirn == "East") {
		prev_action = 3;
	}
	else if(inport_dirn == "South") {
		prev_action = 0;
	}
	else {
		prev_action = 1;
	}
	
	std::cout << "Iterations: " << iter <<std::endl;
	//Updating Q-Table
	//std::cout << "Queueing Delay: " << queueing_delay << std::endl;
	double Qy_min = *std::min_element(Q[my_id][dest_id].begin(), Q[my_id][dest_id].end());
	std::cout << "Q-tavle value before updation: " << Q[prev_router_id][dest_id][prev_action] << std::endl;
	std::cout << "Qy_min: " << Qy_min << " Queueing delay: " << queueing_delay << std::endl;
	Q[prev_router_id][dest_id][prev_action] = Q[prev_router_id][dest_id][prev_action] + LEARNINGRATE * (DISCOUNTRATE*Qy_min + ((int)(queueing_delay)) - Q[prev_router_id][dest_id][prev_action]);
	if(my_id == dest_id){
		Q[prev_router_id][dest_id][prev_action] = Q[prev_router_id][dest_id][prev_action] + LEARNINGRATE * (((int)(queueing_delay)) - Q[prev_router_id][dest_id][prev_action]);
		int outport = lookupRoutingTable(route.vnet, route.net_dest);
	    return outport;
	}

	std::cout << "Q-tavle value after updation: " << Q[prev_router_id][dest_id][prev_action] << std::endl;
	if(curTick() == 100000) {
		std::ofstream f_qTable;
		//std::cout<<"Updating File\n";
		f_qTable.open("Q_Table.txt", std::ios::out | std::ios::trunc);
		for(int i=0;i<NROUTERS;++i) {
			for(int j=0;j<NROUTERS;++j) {
				for(int k=0;k<NACTIONS;++k) {
					f_qTable << Q[i][j][k] << " ";
				}
				f_qTable << "\n";
			}
			f_qTable << "\n";
		}

		//std::cout<<"File Updated\n";
	}
	//std::cout.precision(10);
	//std::cout << std::fixed << EPSILON << std::endl;
    return m_outports_dirn2idx[outport_dirn];
}

int RoutingUnit::outportComputeQ_RoutingTesting(flit *t_flit, int inport, PortDirection inport_dirn) {

	static bool isQTableInitialized = false;
	static std::vector<std::vector<std::vector<double>>> Q(NROUTERS, std::vector<std::vector<double>>(NROUTERS, std::vector<double> (NACTIONS, INT_MAX)));
	if(!isQTableInitialized){
		isQTableInitialized = true;
		if(access("Q_Table.txt", F_OK) == 0) {
			std::ifstream f_qTable {"Q_Table.txt"};
			for(int i=0;i<NROUTERS;++i) {
				for(int j=0;j<NROUTERS;++j) {
					for(int k=0;k<NACTIONS;++k) {
						f_qTable >> Q[i][j][k];
					}
				}
			}
		}
	}

	RouteInfo route = t_flit->get_route();
	PortDirection outport_dirn = "Unknown";
    
	//---Geting source and destination router details
	int M5_VAR_USED num_rows = m_router->get_net_ptr()->getNumRows();
	int num_cols = m_router->get_net_ptr()->getNumCols();
    assert(num_rows > 0 && num_cols > 0);
    int my_id = m_router->get_id();

    int dest_id = route.dest_router;

    int src_id = route.src_router;
	
	//---- Get action
	std::cout << "====================\n";
	std::cout << "My ID: " << my_id << " Src ID: " << src_id << " Dest ID: " << dest_id << std::endl;
	int action = std::distance(Q[my_id][dest_id].begin(), std::min_element(Q[my_id][dest_id].begin(), Q[my_id][dest_id].end()));
	//int id = t_flit->get_id();
	//t_flit->inc_distance();
	//std::cout<<id<<" "<<t_flit->get_distance()<<"\n";
//	distance[id]++;

//	for(int i=0;i<100;++i) {
//		std::cout<<distance[i]<<" ";
//	}
	if(my_id == dest_id){
		std::cout<<"my_id = dest_id\n";
//		distance[id] = 0;
	    int outport = lookupRoutingTable(route.vnet, route.net_dest);	
    	return outport;
	}

	std::cout<<"Action: " << action << std::endl;
	for(int i=0;i<4;++i) {
		std::cout<<Q[my_id][dest_id][i]<<" ";
	}	
	switch(action) {
		case 0: outport_dirn = "North";
				break;

		case 1: outport_dirn = "East";
				break;

		case 2: outport_dirn = "South";
				break;

		case 3: outport_dirn = "West";
				break;

		default:
				std::cout<<"Rchd here\n";
				break;
	}
	std::cout<<"Outport Direction: " << outport_dirn << std::endl;	
	
    return m_outports_dirn2idx[outport_dirn];

}

int
RoutingUnit::outportComputeQ_RoutingPythonTesting(flit *t_flit, int inport, PortDirection inport_dirn) {
	RouteInfo route = t_flit->get_route();
	PortDirection outport_dirn = "Unknown";
    
	//---Geting source and destination router details
	int M5_VAR_USED num_rows = m_router->get_net_ptr()->getNumRows();
	int num_cols = m_router->get_net_ptr()->getNumCols();
    assert(num_rows > 0 && num_cols > 0);

    int my_id = m_router->get_id();

    int dest_id = route.dest_router;

    int src_id = route.src_router;
	//std::cout<<"Current path is "<<std::filesystem::current_path()<<std::endl;

	//char result[1000];
	//ssize_t count = readlink("/proc/self/exe", result, 1000);
	//std::cout<<result<<std::endl;
	
	//setenv("PYTHONPATH", "~/NoC/gem5/src/mem/ruby/network/garnet/", 1);
	static bool isInit = false;
	
	if(!isInit) {
		Py_Initialize();
		isInit = true;
	}
//	Py_Initialize();
	std::cout<<"2- Reached here\n";
    CPyObject pName = PyUnicode_FromString("Q_Routing");
	CPyObject pModule = PyImport_Import(pName);
	
	std::cout<<"3- Reached here\n";
	CPyObject pValue = 0;
	if(pModule)
	{
		std::cout<<"Inside if-else\n";
		CPyObject pFunc = PyObject_GetAttrString(pModule, "start");
		if(pFunc && PyCallable_Check(pFunc))
		{
            CPyObject args = PyTuple_Pack(2,PyLong_FromLong(my_id),PyLong_FromLong(dest_id));
			pValue = PyObject_CallObject(pFunc, args);

			printf("Cpp- Action = %ld\n", PyLong_AsLong(pValue));
		}
		else
		{
			printf("ERROR: function getInteger()\n");
		}

	}
	else
	{
		printf("ERROR: Module not imported\n");
	}

	
	
	int action = (int) PyLong_AsLong(pValue);
//	Py_Finalize();
//	
	//std::cout<<"Exiting\n";
//	if(action == 0) {
//		outport_dirn = "North";
//	}
//	else if(action == 1) {
//		outport_dirn = "East";
//	}
//	else if(action == 2) {
//		outport_dirn = "South";
//	}
//	else {
//		outport_dirn = "West";
//	}
	std::cout<<"Action: "<<action<<std::endl;
	switch(action) {
		case 0: outport_dirn = "North";
				break;

		case 1: outport_dirn = "East";
				break;

		case 2: outport_dirn = "South";
				break;

		case 3: outport_dirn = "West";
				break;

		default:
				std::cout<<"Rchd here\n";
				break;
	}
	
	std::cout<<"4- Reached here"<<std::endl;
	//Py_Finalize();
	std::cout<<"5- Reached here"<<std::endl;
	std::cout<<outport_dirn<<std::endl;
	auto x = m_outports_dirn2idx[outport_dirn];
	std::cout<<"6- Reached here "<<x<<std::endl;

//	return m_outports_dirn2idx[outport_dirn];
	auto x1 = this->outportComputeQ_RoutingTesting(t_flit, inport, inport_dirn);
	std::cout<<"C function "<<x1<<std::endl;
	return x;

	/*
	
	std::ofstream file;
	file.open("/home/b170330cs/NoC/gem5/input.txt");
	file << my_id << "\n";
	file << dest_id << "\n";
	file.close();
	
	
//	Reading from file outputed by Python script
	int action = -1;	
	while (1) {
		//std::cout<<"\nWaiting for file\n";
		std::string filename = "/home/b170330cs/NoC/gem5/action.txt";
		struct stat buffer;
		if(stat(filename.c_str(), &buffer) == 0 || curTick() == 100000) {
			break;
		}
	}
	std::cout<<"Action file read\n";
	do {
//        std::cout<<"Looping \n";
        std::ifstream out("/home/b170330cs/NoC/gem5/action.txt");
        out >> action;
        out.close();
    }while(action == -1);
	std::cout<<"Action "<<action<<"\n";
	//std::cout<<"\nFile created\n";
	//std::fstream out("/home/rohitr/NoC-Routing/action.txt",std::ios_base::in);
	
	//out >> action;
	std::cout<<"Action = "<<action<<"\n";
	if(std::remove("/home/b170330cs/NoC/gem5/action.txt") == 0) {
		std::cout<<"File removed\n\n";
	}
//	-----
	switch(action) {
		case 0: outport_dirn = "North";
				break;
		case 1: outport_dirn = "East";
				break;
		case 2: outport_dirn = "South";
				break;
		case 3: outport_dirn = "West";
				break;
		default:
				std::cout<<"Rchd here\n";
				break;
	}
	
	auto x = m_outports_dirn2idx[outport_dirn];
	return x;
*/
}


int RoutingUnit::outportComputeDQNvcPython(flit *t_flit, int inport, PortDirection inport_dirn) {
	RouteInfo route = t_flit->get_route();
	PortDirection outport_dirn = "Unknown";
    
	Tick src_queueing_delay = t_flit->get_src_delay();
    Tick dest_queueing_delay = (curTick() - t_flit->get_dequeue_time());
    Tick queueing_delay = src_queueing_delay + dest_queueing_delay;

	//---Geting source and destination router details
	int M5_VAR_USED num_rows = m_router->get_net_ptr()->getNumRows();
	int num_cols = m_router->get_net_ptr()->getNumCols();
    assert(num_rows > 0 && num_cols > 0);

    int my_id = m_router->get_id();
    int my_x = my_id % num_cols;
    int my_y = my_id / num_cols;

//	std::cout << "Cols:" << num_cols << " Rows: " << num_rows << "\n";

    int dest_id = route.dest_router;

    int src_id = route.src_router;
	
	static bool isInit = false;

	int action = -1;
	int prev_router_id;

	int temp_x = 0;
	int temp_y = 0;
	
	if(inport_dirn == "North"){
		if(my_y < num_rows - 1) {
			temp_y = my_y + 1;
			temp_x = my_x;
		}
	}
	else if(inport_dirn == "South") {
		if(my_y > 0) {
			temp_y = my_y - 1;
			temp_x = my_x;
		}
	
	}
	else if(inport_dirn == "East") {
		if(my_x < num_cols -1) {
			temp_x = my_x + 1;
			temp_y = my_y;
		}
	}
	else if(inport_dirn == "West"){
		if(my_x > 0) {
			temp_x = my_x - 1;
			temp_y = my_y;
		}
	
	}
	else{
	}

	prev_router_id = temp_y * num_cols + temp_x;

	int prev_action = 0;
	if(inport_dirn == "North") {
		prev_action = 2;
	}
	else if(inport_dirn == "East") {
		prev_action = 3;
	}
	else if(inport_dirn == "South") {
		prev_action = 0;
	}
	else {
		prev_action = 1;
	}
	
	char* directions[] = {"North", "East", "South", "West"};
	int free_vcs[4];
	for(int i=0; i <= 3; i++){
	    auto outputunit = m_router->getOutputUnit(m_outports_dirn2idx[directions[i]]);
		free_vcs[i] = outputunit->count_free_vc(route.vnet);
	}

//	New Changes
//	-----
	if(!isInit) {
		srand(time(NULL));
		isInit = true;
	}
	std::cout<<"Printing in file\n";
	
	std::ofstream file;
	file.open("/home/b170330cs/NoC/gem5/input.txt");
	file << my_id << "\n";
	file << src_id << "\n";
	file << dest_id << "\n";
	file << prev_router_id <<"\n";
	file << prev_action << "\n";
	file << queueing_delay<< "\n";
	file << curTick() << "\n";
	file << free_vcs[0] << "\n";
	file << free_vcs[1] << "\n";
	file << free_vcs[2] << "\n";
	file << free_vcs[3];
	file.close();
	
	if(my_id == dest_id){
	    int outport = lookupRoutingTable(route.vnet, route.net_dest);	
		while (1) {
			//std::cout<<"\nWaiting for file\n";
			std::string filename = "/home/b170330cs/NoC/gem5/action.txt";
			struct stat buffer;
			if(stat(filename.c_str(), &buffer) == 0 || curTick() == 100000) {
				break;
			}
		}
		if(std::remove("/home/b170330cs/NoC/gem5/action.txt") == 0) {
			std::cout<<"File removed\n";
		}
		std::cout << "If myid == destid\n";
    	return outport;
	}


//	Reading from file outputed by Python script
	
	while (1) {
		//std::cout<<"\nWaiting for file\n";
		std::string filename = "/home/b170330cs/NoC/gem5/action.txt";
		struct stat buffer;
		if(stat(filename.c_str(), &buffer) == 0 || curTick() == 100000) {
			break;
		}
	}

	do {
//        std::cout<<"Looping \n";
        std::ifstream out("/home/b170330cs/NoC/gem5/action.txt");

        out >> action;

        out.close();
    }while(action == -1);


	//std::cout<<"\nFile created\n";
	//std::fstream out("/home/rohitr/NoC-Routing/action.txt",std::ios_base::in);
	
	//out >> action;

	std::cout<<"Action = "<<action<<"\n";
	std:: cout << "Tick = " << curTick() << "\n";
	std::ofstream fl("/home/b170330cs/NoC/gem5/cppaction.txt", std::ios::app);
	fl << action << "\n";
	fl.close();
	if(std::remove("/home/b170330cs/NoC/gem5/action.txt") == 0) {
		std::cout<<"File removed\n\n";
	}
//	-----
	switch(action) {
		case 0: outport_dirn = "North";
				break;

		case 1: outport_dirn = "East";
				break;

		case 2: outport_dirn = "South";
				break;

		case 3: outport_dirn = "West";
				break;

		default:
				std::cout<<"Rchd here\n";
				break;
	}
	


	auto x = m_outports_dirn2idx[outport_dirn];
	return x;
}


int RoutingUnit::outportComputeDQNhopsPython(flit *t_flit, int inport, PortDirection inport_dirn) {
	RouteInfo route = t_flit->get_route();
	PortDirection outport_dirn = "Unknown";
    
	Tick src_queueing_delay = t_flit->get_src_delay();
    Tick dest_queueing_delay = (curTick() - t_flit->get_dequeue_time());
    Tick queueing_delay = src_queueing_delay + dest_queueing_delay;

	//---Geting source and destination router details
	int M5_VAR_USED num_rows = m_router->get_net_ptr()->getNumRows();
	int num_cols = m_router->get_net_ptr()->getNumCols();
    assert(num_rows > 0 && num_cols > 0);

    int my_id = m_router->get_id();
    int my_x = my_id % num_cols;
    int my_y = my_id / num_cols;

//	std::cout << "Cols:" << num_cols << " Rows: " << num_rows << "\n";

    int dest_id = route.dest_router;

    int src_id = route.src_router;
	
	static bool isInit = false;

	int action = -1;
	int prev_router_id;

	int temp_x = 0;
	int temp_y = 0;
	
	
	if(inport_dirn == "North"){
		if(my_y < num_rows - 1) {
			temp_y = my_y + 1;
			temp_x = my_x;
		}
	}
	else if(inport_dirn == "South") {
		if(my_y > 0) {
			temp_y = my_y - 1;
			temp_x = my_x;
		}
	
	}
	else if(inport_dirn == "East") {
		if(my_x < num_cols -1) {
			temp_x = my_x + 1;
			temp_y = my_y;
		}
	}
	else if(inport_dirn == "West"){
		if(my_x > 0) {
			temp_x = my_x - 1;
			temp_y = my_y;
		}
	
	}
	else{
	}

	prev_router_id = temp_y * num_cols + temp_x;

	int prev_action = 0;
	if(inport_dirn == "North") {
		prev_action = 2;
	}
	else if(inport_dirn == "East") {
		prev_action = 3;
	}
	else if(inport_dirn == "South") {
		prev_action = 0;
	}
	else {
		prev_action = 1;
	}
	
	char* directions[] = {"North", "East", "South", "West"};
	int free_vcs[4];
	for(int i=0; i <= 3; i++){
	    auto outputunit = m_router->getOutputUnit(m_outports_dirn2idx[directions[i]]);
		free_vcs[i] = outputunit->count_free_vc(route.vnet);
	}

	int past_dist = t_flit->get_distance();
//	New Changes
//	-----
	if(!isInit) {
		srand(time(NULL));
		isInit = true;
	}
	std::cout<<"Printing in file\n";
	
	std::ofstream file;
	file.open("/home/b170330cs/NoC/gem5/input.txt");
	file << my_id << "\n";
	file << src_id << "\n";
	file << dest_id << "\n";
	file << prev_router_id <<"\n";
	file << prev_action << "\n";
	file << queueing_delay<< "\n";
	file << curTick() << "\n";
	file << free_vcs[0] << "\n";
	file << free_vcs[1] << "\n";
	file << free_vcs[2] << "\n";
	file << free_vcs[3] << "\n";
	file << past_dist;
	file.close();
	
	if(my_id == dest_id){
	    int outport = lookupRoutingTable(route.vnet, route.net_dest);	
		while (1) {
			//std::cout<<"\nWaiting for file\n";
			std::string filename = "/home/b170330cs/NoC/gem5/action.txt";
			struct stat buffer;
			if(stat(filename.c_str(), &buffer) == 0 || curTick() == 100000) {
				break;
			}
		}
		if(std::remove("/home/b170330cs/NoC/gem5/action.txt") == 0) {
			std::cout<<"File removed\n";
		}
		std::cout << "If myid == destid\n";
    	return outport;
	}


//	Reading from file outputed by Python script
	
	while (1) {
		//std::cout<<"\nWaiting for file\n";
		std::string filename = "/home/b170330cs/NoC/gem5/action.txt";
		struct stat buffer;
		if(stat(filename.c_str(), &buffer) == 0 || curTick() == 100000) {
			break;
		}
	}

	do {
//        std::cout<<"Looping \n";
        std::ifstream out("/home/b170330cs/NoC/gem5/action.txt");

        out >> action;

        out.close();
    }while(action == -1);


	//std::cout<<"\nFile created\n";
	//std::fstream out("/home/rohitr/NoC-Routing/action.txt",std::ios_base::in);
	
	//out >> action;

	std::cout<<"Action = "<<action<<"\n";
	std:: cout << "Tick = " << curTick() << "\n";
	std::ofstream fl("/home/b170330cs/NoC/gem5/cppaction.txt", std::ios::app);
	fl << action << "\n";
	fl.close();
	if(std::remove("/home/b170330cs/NoC/gem5/action.txt") == 0) {
		std::cout<<"File removed\n\n";
	}
//	-----
	switch(action) {
		case 0: outport_dirn = "North";
				break;

		case 1: outport_dirn = "East";
				break;

		case 2: outport_dirn = "South";
				break;

		case 3: outport_dirn = "West";
				break;

		default:
				std::cout<<"Rchd here\n";
				break;
	}
	
	t_flit->inc_distance();

	auto x = m_outports_dirn2idx[outport_dirn];
	return x;
}




int RoutingUnit::outportComputeDQNPython(flit *t_flit, int inport, PortDirection inport_dirn) {
	RouteInfo route = t_flit->get_route();
	PortDirection outport_dirn = "Unknown";
    
	Tick src_queueing_delay = t_flit->get_src_delay();
    Tick dest_queueing_delay = (curTick() - t_flit->get_dequeue_time());
    Tick queueing_delay = src_queueing_delay + dest_queueing_delay;

	//---Geting source and destination router details
	int M5_VAR_USED num_rows = m_router->get_net_ptr()->getNumRows();
	int num_cols = m_router->get_net_ptr()->getNumCols();
    assert(num_rows > 0 && num_cols > 0);

    int my_id = m_router->get_id();
    int my_x = my_id % num_cols;
    int my_y = my_id / num_cols;

//	std::cout << "Cols:" << num_cols << " Rows: " << num_rows << "\n";

    int dest_id = route.dest_router;

    int src_id = route.src_router;
	
	static bool isInit = false;

	int action = -1;
	int prev_router_id;

	int temp_x = 0;
	int temp_y = 0;
	
	if(inport_dirn == "North"){
		if(my_y < num_rows - 1) {
			temp_y = my_y + 1;
			temp_x = my_x;
		}
	}
	else if(inport_dirn == "South") {
		if(my_y > 0) {
			temp_y = my_y - 1;
			temp_x = my_x;
		}
	
	}
	else if(inport_dirn == "East") {
		if(my_x < num_cols -1) {
			temp_x = my_x + 1;
			temp_y = my_y;
		}
	}
	else if(inport_dirn == "West"){
		if(my_x > 0) {
			temp_x = my_x - 1;
			temp_y = my_y;
		}
	
	}
	else{
	}

	prev_router_id = temp_y * num_cols + temp_x;

	int prev_action = 0;
	if(inport_dirn == "North") {
		prev_action = 2;
	}
	else if(inport_dirn == "East") {
		prev_action = 3;
	}
	else if(inport_dirn == "South") {
		prev_action = 0;
	}
	else {
		prev_action = 1;
	}

//	New Changes
//	-----
	if(!isInit) {
		srand(time(NULL));
		isInit = true;
	}
	std::cout<<"Printing in file\n";
	
	std::ofstream file;
	file.open("/home/b170330cs/NoC/gem5/input.txt");
	file << my_id << "\n";
	file << src_id << "\n";
	file << dest_id << "\n";
	file << prev_router_id <<"\n";
	file << prev_action << "\n";
	file << queueing_delay<< "\n";
	file << curTick();
	file.close();
	
	if(my_id == dest_id){
	    int outport = lookupRoutingTable(route.vnet, route.net_dest);	
		while (1) {
			//std::cout<<"\nWaiting for file\n";
			std::string filename = "/home/b170330cs/NoC/gem5/action.txt";
			struct stat buffer;
			if(stat(filename.c_str(), &buffer) == 0 || curTick() == 100000) {
				break;
			}
		}
		if(std::remove("/home/b170330cs/NoC/gem5/action.txt") == 0) {
			std::cout<<"File removed\n";
		}
		std::cout << "If myid == destid\n";
    	return outport;
	}


//	Reading from file outputed by Python script
	
	while (1) {
		//std::cout<<"\nWaiting for file\n";
		std::string filename = "/home/b170330cs/NoC/gem5/action.txt";
		struct stat buffer;
		if(stat(filename.c_str(), &buffer) == 0 || curTick() == 100000) {
			break;
		}
	}

	do {
//        std::cout<<"Looping \n";
        std::ifstream out("/home/b170330cs/NoC/gem5/action.txt");

        out >> action;

        out.close();
    }while(action == -1);


	//std::cout<<"\nFile created\n";
	//std::fstream out("/home/rohitr/NoC-Routing/action.txt",std::ios_base::in);
	
	//out >> action;

	std::cout<<"Action = "<<action<<"\n";
	std:: cout << "Tick = " << curTick() << "\n";
	std::ofstream fl("/home/b170330cs/NoC/gem5/cppaction.txt", std::ios::app);
	fl << action << "\n";
	fl.close();
	if(std::remove("/home/b170330cs/NoC/gem5/action.txt") == 0) {
		std::cout<<"File removed\n\n";
	}
//	-----
	switch(action) {
		case 0: outport_dirn = "North";
				break;

		case 1: outport_dirn = "East";
				break;

		case 2: outport_dirn = "South";
				break;

		case 3: outport_dirn = "West";
				break;

		default:
				std::cout<<"Rchd here\n";
				break;
	}
	


	auto x = m_outports_dirn2idx[outport_dirn];
	return x;
}

int RoutingUnit::outportComputeDQNPythonTesting(flit *t_flit, int inport, PortDirection inport_dirn) {
	RouteInfo route = t_flit->get_route();
	PortDirection outport_dirn = "Unknown";
    
	Tick src_queueing_delay = t_flit->get_src_delay();
    Tick dest_queueing_delay = (curTick() - t_flit->get_dequeue_time());
    Tick queueing_delay = src_queueing_delay + dest_queueing_delay;

	//---Geting source and destination router details
	int M5_VAR_USED num_rows = m_router->get_net_ptr()->getNumRows();
	int num_cols = m_router->get_net_ptr()->getNumCols();
    assert(num_rows > 0 && num_cols > 0);

    int my_id = m_router->get_id();
    int my_x = my_id % num_cols;
    int my_y = my_id / num_cols;

//	std::cout << "Cols:" << num_cols << " Rows: " << num_rows << "\n";

    int dest_id = route.dest_router;

    int src_id = route.src_router;
	
	static bool isInit = false;

	int action = -1;
	int prev_router_id;

	
	std::ofstream file;
	file.open("/home/b170330cs/NoC/gem5/input.txt");
	file << my_id << "\n";
	file << dest_id << "\n";
	file.close();
	

//	Reading from file outputed by Python script
	
	while (1) {
		//std::cout<<"\nWaiting for file\n";
		std::string filename = "/home/b170330cs/NoC/gem5/action.txt";
		struct stat buffer;
		if(stat(filename.c_str(), &buffer) == 0 || curTick() == 100000) {
			break;
		}
	}

	do {
//        std::cout<<"Looping \n";
        std::ifstream out("/home/b170330cs/NoC/gem5/action.txt");

        out >> action;

        out.close();
    }while(action == -1);


	//std::cout<<"\nFile created\n";
	//std::fstream out("/home/rohitr/NoC-Routing/action.txt",std::ios_base::in);
	
	//out >> action;

	//std::cout<<"Action = "<<action<<"\n";
	std:: cout << "Tick = " << curTick() << "\n";
	std::ofstream fl("/home/b170330cs/NoC/gem5/cppaction.txt", std::ios::app);
	fl << action << "\n";
	fl.close();
	if(std::remove("/home/b170330cs/NoC/gem5/action.txt") == 0) {
		//std::cout<<"File removed\n\n";
	}
//	-----
	switch(action) {
		case 0: outport_dirn = "North";
				break;

		case 1: outport_dirn = "East";
				break;

		case 2: outport_dirn = "South";
				break;

		case 3: outport_dirn = "West";
				break;

		default:
				std::cout<<"Rchd here\n";
				break;
	}
	
	std::ofstream file_t;
	file_t.open("/home/b170330cs/NoC/gem5/DQN_path.txt",std::ios_base::app);

	
	file_t <<"Current router id: "<<my_id<<"\tInport direction: "<<inport_dirn<<"\tOutport Dirn: "<<outport_dirn<<"\n";
    file_t.close();
	

	auto x = m_outports_dirn2idx[outport_dirn];
	return x;
}



int RoutingUnit::outportComputeDQNvcPythonTesting(flit *t_flit, int inport, PortDirection inport_dirn) {
	RouteInfo route = t_flit->get_route();
	PortDirection outport_dirn = "Unknown";
    
	Tick src_queueing_delay = t_flit->get_src_delay();
    Tick dest_queueing_delay = (curTick() - t_flit->get_dequeue_time());
    Tick queueing_delay = src_queueing_delay + dest_queueing_delay;

	//---Geting source and destination router details
	int M5_VAR_USED num_rows = m_router->get_net_ptr()->getNumRows();
	int num_cols = m_router->get_net_ptr()->getNumCols();
    assert(num_rows > 0 && num_cols > 0);

    int my_id = m_router->get_id();
    int my_x = my_id % num_cols;
    int my_y = my_id / num_cols;

//	std::cout << "Cols:" << num_cols << " Rows: " << num_rows << "\n";

    int dest_id = route.dest_router;

    int src_id = route.src_router;
	
	static bool isInit = false;

	int action = -1;
	int prev_router_id;

	char* directions[] = {"North", "East", "South", "West"};
	int free_vcs[4];
	for(int i=0; i <= 3; i++){
		std::cout<<directions[i]<<" ";
		auto outputunit = m_router->getOutputUnit(m_outports_dirn2idx[directions[i]]);
		std::cout<<outputunit->count_free_vc(route.vnet)<<"\n";
		free_vcs[i] = outputunit->count_free_vc(route.vnet);
	}
	std::cout<<"Free vcs: ";
	for(int i=0;i<4;++i) {
		std::cout<<free_vcs[i]<<" ";
	}
	std::cout<<"\n";
	std::ofstream file;
	file.open("/home/b170330cs/NoC/gem5/input.txt");
	file << my_id << "\n";
	file << dest_id << "\n";
	file << free_vcs[0] << "\n";
	file << free_vcs[1] << "\n";
	file << free_vcs[2] << "\n";
	file << free_vcs[3] << "\n";
	file.close();
	

//	Reading from file outputed by Python script
	
	while (1) {
		//std::cout<<"\nWaiting for file\n";
		std::string filename = "/home/b170330cs/NoC/gem5/action.txt";
		struct stat buffer;
		if(stat(filename.c_str(), &buffer) == 0 || curTick() == 100000) {
			break;
		}
	}

	do {
//        std::cout<<"Looping \n";
        std::ifstream out("/home/b170330cs/NoC/gem5/action.txt");

        out >> action;

        out.close();
    }while(action == -1);


	//std::cout<<"\nFile created\n";
	//std::fstream out("/home/rohitr/NoC-Routing/action.txt",std::ios_base::in);
	
	//out >> action;

	//std::cout<<"Action = "<<action<<"\n";
	std:: cout << "Tick = " << curTick() << "\n";
	std::ofstream fl("/home/b170330cs/NoC/gem5/cppaction.txt", std::ios::app);
	fl << action << "\n";
	fl.close();
	if(std::remove("/home/b170330cs/NoC/gem5/action.txt") == 0) {
		//std::cout<<"File removed\n\n";
	}
//	-----
	switch(action) {
		case 0: outport_dirn = "North";
				break;

		case 1: outport_dirn = "East";
				break;

		case 2: outport_dirn = "South";
				break;

		case 3: outport_dirn = "West";
				break;

		default:
				std::cout<<"Rchd here\n";
				break;
	}
	


	auto x = m_outports_dirn2idx[outport_dirn];
	return x;
}



int RoutingUnit::outportComputeDQNhopsPythonTesting(flit *t_flit, int inport, PortDirection inport_dirn) {
	RouteInfo route = t_flit->get_route();
	PortDirection outport_dirn = "Unknown";
    
	Tick src_queueing_delay = t_flit->get_src_delay();
    Tick dest_queueing_delay = (curTick() - t_flit->get_dequeue_time());
    Tick queueing_delay = src_queueing_delay + dest_queueing_delay;

	//---Geting source and destination router details
	int M5_VAR_USED num_rows = m_router->get_net_ptr()->getNumRows();
	int num_cols = m_router->get_net_ptr()->getNumCols();
    assert(num_rows > 0 && num_cols > 0);

    int my_id = m_router->get_id();
    int my_x = my_id % num_cols;
    int my_y = my_id / num_cols;

//	std::cout << "Cols:" << num_cols << " Rows: " << num_rows << "\n";

    int dest_id = route.dest_router;

    int src_id = route.src_router;
	
	static bool isInit = false;

	int action = -1;
	int prev_router_id;

	int past_dist = t_flit->get_distance();	

	int free_vcs[4];
	for(int i=0; i < 3; i++){
		char* directions[] = {"North", "East", "South", "West"};
	    auto outputunit = m_router->getOutputUnit(m_outports_dirn2idx[directions[i]]);
		free_vcs[i] = outputunit->count_free_vc(route.vnet);
	}

	std::ofstream file;
	file.open("/home/b170330cs/NoC/gem5/input.txt");
	file << my_id << "\n";
	file << dest_id << "\n";
	file << free_vcs[0] << "\n";
	file << free_vcs[1] << "\n";
	file << free_vcs[2] << "\n";
	file << free_vcs[3] << "\n";
	file << past_dist << "\n";
	file.close();
	
//	Reading from file outputed by Python script
	
	while (1) {
		//std::cout<<"\nWaiting for file\n";
		std::string filename = "/home/b170330cs/NoC/gem5/action.txt";
		struct stat buffer;
		if(stat(filename.c_str(), &buffer) == 0 || curTick() == 100000) {
			break;
		}
	}

	do {
//        std::cout<<"Looping \n";
        std::ifstream out("/home/b170330cs/NoC/gem5/action.txt");

        out >> action;

        out.close();
    }while(action == -1);


	//std::cout<<"\nFile created\n";
	//std::fstream out("/home/rohitr/NoC-Routing/action.txt",std::ios_base::in);
	
	//out >> action;

	//std::cout<<"Action = "<<action<<"\n";
	std:: cout << "Tick = " << curTick() << "\n";
	std::ofstream fl("/home/b170330cs/NoC/gem5/cppaction.txt", std::ios::app);
	fl << action << "\n";
	fl.close();
	if(std::remove("/home/b170330cs/NoC/gem5/action.txt") == 0) {
		//std::cout<<"File removed\n\n";
	}
//	-----
	switch(action) {
		case 0: outport_dirn = "North";
				break;

		case 1: outport_dirn = "East";
				break;

		case 2: outport_dirn = "South";
				break;

		case 3: outport_dirn = "West";
				break;

		default:
				std::cout<<"Rchd here\n";
				break;
	}
	
	t_flit->inc_distance();

	auto x = m_outports_dirn2idx[outport_dirn];
	return x;
}



// Template for implementing custom routing algorithm
// using port directions. (Example adaptive)
int
RoutingUnit::outportComputeCustom(RouteInfo route, int inport, PortDirection inport_dirn)
{
    panic("%s placeholder executed", __FUNCTION__);
}
