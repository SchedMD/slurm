/*****************************************************************************\
 *  bgl_switch_connections.c
 * 
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Dan Phung <phung4@llnl.gov>
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

/**
 * connect the given switch up with the given connections
 */
void _connect(rm_partition_t *my_part, rm_switch_t *my_switch,
	      rm_connection_t *conn1, rm_connection_t *conn2, rm_connection_t *conn3,
	      int first)
{
	// rm_get_data(bgl,RM_FirstSwitch,&my_switch);
	// rm_get_data(bgl,RM_NextSwitch,&my_switch);
	
	if (first){
		rm_set_data(my_switch,RM_SwitchFirstConnection,conn1);
		rm_set_data(my_switch,RM_SwitchSecondConnection,conn2);
		rm_set_data(my_switch,RM_SwitchThirdConnection,conn3);
		rm_set_data(my_part,RM_PartFirstSwitch,my_switch);
	} else {
		rm_set_data(my_switch,RM_SwitchFirstConnection,conn1);
		rm_set_data(my_switch,RM_SwitchSecondConnection,conn2);
		rm_set_data(my_switch,RM_SwitchThirdConnection,conn3);
		rm_set_data(my_part,RM_PartNextSwitch,my_switch);
	}  
}

/**
 * connect the given switch up in the "A" pattern
 *       0  1
 *    /--|--|--\
 *    |  /  \  |
 *  2 --/    \-- 5
 *    |  /--\  |
 *    \__|__|__/
 *       3  4
 */
void connect_switch_A(rm_partition_t *my_part, rm_switch_t *my_switch,
		      int first)
{
	rm_connection_t conn1, conn2, conn3;

	conn1.p1 = RM_PORT_S0; 
	conn1.p2 = RM_PORT_S2;
	conn1.part_id = NULL;
	conn1.usage = RM_CONNECTION_USED;

	conn2.p1 = RM_PORT_S1; 
	conn2.p2 = RM_PORT_S5;
	conn2.part_id = NULL;
	conn2.usage = RM_CONNECTION_USED;

	conn3.p1 = RM_PORT_S3; 
	conn3.p2 = RM_PORT_S4;
	conn3.part_id = NULL;
	conn3.usage = RM_CONNECTION_USED;

	connect(my_part, my_switch, &conn1, &conn2, &conn3, first);
}

/**
 * connect the given switch up in the "B" pattern
 *       0  1
 *    /--|--|--\
 *    |  \  /  |
 *  2 ----\/---- 5
 *    |   /\   |
 *    \__|__|__/
 *       3  4
 */
void connect_switch_B(rm_partition_t *my_part, rm_switch_t *my_switch,
		      int first)
{
	rm_connection_t conn1, conn2, conn3;

	conn1.p1 = RM_PORT_S0; 
	conn1.p2 = RM_PORT_S4;
	conn1.part_id = NULL;
	conn1.usage = RM_CONNECTION_USED;

	conn2.p1 = RM_PORT_S1; 
	conn2.p2 = RM_PORT_S3;
	conn2.part_id = NULL;
	conn2.usage = RM_CONNECTION_USED;

	conn3.p1 = RM_PORT_S2; 
	conn3.p2 = RM_PORT_S5;
	conn3.part_id = NULL;
	conn3.usage = RM_CONNECTION_USED;
  
	connect(my_part, my_switch, &conn1, &conn2, &conn3, first);
}

/**
 * connect the given switch up in the "C" pattern
 *       0  1
 *    /--|--|--\
 *    |  \  \  |
 *  5 --\ \  \-- 2
 *    |  \ \   |
 *    \__|__|__/
 *       3  4
 */
void connect_switch_C(rm_partition_t *my_part, rm_switch_t *my_switch,
		      int first)
{
	rm_connection_t conn1, conn2, conn3;

	conn1.p1 = RM_PORT_S0; 
	conn1.p2 = RM_PORT_S4;
	conn1.part_id = NULL;
	conn1.usage = RM_CONNECTION_USED;

	conn2.p1 = RM_PORT_S1; 
	conn2.p2 = RM_PORT_S5;
	conn2.part_id = NULL;
	conn2.usage = RM_CONNECTION_USED;

	conn3.p1 = RM_PORT_S2; 
	conn3.p2 = RM_PORT_S3;
	conn3.part_id = NULL;
	conn3.usage = RM_CONNECTION_USED;
  
	connect(my_part, my_switch, &conn1, &conn2, &conn3, first);
}

/**
 * connect the given switch up in the "D" pattern
 *       0  1
 *    /--|--|--\
 *    |  /  /  |
 *  2 --/  / /-- 5
 *    |   / /  |
 *    \__|__|__/
 *       3  4
 */
void connect_switch_D(rm_partition_t *my_part, rm_switch_t *my_switch,
		      int first)
{
	rm_connection_t conn1, conn2, conn3;

	conn1.p1 = RM_PORT_S0; 
	conn1.p2 = RM_PORT_S2;
	conn1.part_id = NULL;
	conn1.usage = RM_CONNECTION_USED;

	conn2.p1 = RM_PORT_S1;
	conn2.p2 = RM_PORT_S3;
	conn2.part_id = NULL;
	conn2.usage = RM_CONNECTION_USED;

	conn3.p1 = RM_PORT_S4; 
	conn3.p2 = RM_PORT_S5;
	conn3.part_id = NULL;
	conn3.usage = RM_CONNECTION_USED;
  
	connect(my_part, my_switch, &conn1, &conn2, &conn3, first);
}

/**
 * connect the given switch up in the "E" pattern (loopback)
 *       0  1
 *    /--|--|--\
 *    |  \__/  |
 *  2 ---------- 5
 *    |  /--\  |
 *    \__|__|__/
 *       3  4
 */
void connect_switch_E(rm_partition_t *my_part, rm_switch_t *my_switch,
		      int first)
{
	rm_connection_t conn1, conn2, conn3;

	conn1.p1 = RM_PORT_S0; 
	conn1.p2 = RM_PORT_S1;
	conn1.part_id = NULL;
	conn1.usage = RM_CONNECTION_USED;

	conn2.p1 = RM_PORT_S2; 
	conn2.p2 = RM_PORT_S5;
	conn2.part_id = NULL;
	conn2.usage = RM_CONNECTION_USED;

	conn3.p1 = RM_PORT_S3; 
	conn3.p2 = RM_PORT_S4;
	conn3.part_id = NULL;
	conn3.usage = RM_CONNECTION_USED;
  
	connect(my_part, my_switch, &conn1, &conn2, &conn3, first);
}

/**
 * connect the given switch up in the "F" pattern (loopback)
 *       0  1
 *    /--|--|--\
 *    |  \__/  |
 *  2 --\    /-- 5
 *    |  \  /  |
 *    \__|__|__/
 *       3  4
 */
void connect_switch_F(rm_partition_t *my_part, rm_switch_t *my_switch,
		      int first)
{
	rm_connection_t conn1, conn2, conn3;

	conn1.p1 = RM_PORT_S0; 
	conn1.p2 = RM_PORT_S1;
	conn1.part_id = NULL;
	conn1.usage = RM_CONNECTION_USED;

	conn2.p1 = RM_PORT_S2; 
	conn2.p2 = RM_PORT_S3;
	conn2.part_id = NULL;
	conn2.usage = RM_CONNECTION_USED;

	conn3.p1 = RM_PORT_S4; 
	conn3.p2 = RM_PORT_S5;
	conn3.part_id = NULL;
	conn3.usage = RM_CONNECTION_USED;
  
	connect(my_part, my_switch, &conn1, &conn2, &conn3, first);
}


/**
 * connect the node to the next node (higher up number)
 *       0  1
 *    /--|--|--\
 *    |    /   |
 *  2 -   /    - 5
 *    |  /     |
 *    \__|__|__/
 *       3  4
 */
void connect_next(rm_partition_t *my_part, rm_switch_t *my_switch)
{
	rm_connection_t conn1, conn2, conn3;
	int first = 0;

	conn1.p1 = RM_PORT_S1;
	conn1.p2 = RM_PORT_S3;
	conn1.part_id = NULL;
	conn1.usage = RM_CONNECTION_USED;

	conn2.p1 = RM_PORT_S0; 
	conn2.p2 = RM_PORT_S2;
	conn2.part_id = NULL;
	conn2.usage = RM_CONNECTION_NOT_USED;

	conn3.p1 = RM_PORT_S4; 
	conn3.p2 = RM_PORT_S5;
	conn3.part_id = NULL;
	conn3.usage = RM_CONNECTION_NOT_USED;

	_connect(my_part, my_switch, &conn1, &conn2, &conn3, first);
}

/**
 * connect the given switch up to the previous node
 *       0  1
 *    /--|--|--\
 *    |  \     |
 *  2 -   \    - 5
 *    |    \   |
 *    \__|__|__/
 *       3  4
 */
void connect_prev(rm_partition_t *my_part, rm_switch_t *my_switch)
{
	rm_connection_t conn1, conn2, conn3;
	int first = 0;

	conn1.p1 = RM_PORT_S0;
	conn1.p2 = RM_PORT_S4;
	conn1.part_id = NULL;
	conn1.usage = RM_CONNECTION_USED;

	conn2.p1 = RM_PORT_S2; 
	conn2.p2 = RM_PORT_S3;
	conn2.part_id = NULL;
	conn2.usage = RM_CONNECTION_NOT_USED;

	conn3.p1 = RM_PORT_S1; 
	conn3.p2 = RM_PORT_S5;
	conn3.part_id = NULL;
	conn3.usage = RM_CONNECTION_NOT_USED;

	connect(my_part, my_switch, &conn1, &conn2, &conn3, first);
}
