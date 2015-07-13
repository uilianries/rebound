/**
 * @file 	tree.c
 * @brief 	Tree routine, initializing and updating trees.
 * @author 	Shangfei Liu <liushangfei@pku.edu.cn> 
 * @author  Hanno Rein <hanno@hanno-rein.de>
 * 
 * @section 	LICENSE
 * Copyright (c) 2011 Hanno Rein, Shangfei Liu
 *
 * This file is part of rebound.
 *
 * rebound is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * rebound is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with rebound.  If not, see <http://www.gnu.org/licenses/>.
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <time.h>
#include "particle.h"
#include "rebound.h"
#include "boundary.h"
#include "tree.h"
#include "communication_mpi.h"


/**
  * Given the index of a particle and a pointer to a node cell, the function returns the index
  * of the octant which the particle belongs to.
  *
  * @param pt is the index of a particle.
  * @param node is the pointer to a node cell. 
  */
int tree_get_octant_for_particle_in_cell(const struct Particle p, struct cell *node);

/**
  * This function adds a particle to the octant[o] of a node. 
  *
  * If node is NULL, the function allocate memory for it and calculate its geometric properties. 
  * As a leaf node, node->pt = pt. 
  *
  * If node already exists, the function calls itself recursively until reach a leaf node.
  * The leaf node would be divided into eight octants, then it puts the leaf-node hosting particle 
  * and the new particle into these octants. 
  * 
  * @param node is the pointer to a node cell
  * @param pt is the index of a particle.
  * @param parent is the pointer to the parent cell of node. if node is a root, then parent
  * is set to be NULL.
  * @param o is the index of the octant of the node which particles[pt] belongs to.
  */
struct cell *tree_add_particle_to_cell(struct Rebound* const r, struct cell *node, int pt, struct cell *parent, int o);

void tree_add_particle_to_tree(struct Rebound* const r, int pt){
	if (r->tree_root==NULL){
		r->tree_root = calloc(r->root_nx*r->root_ny*r->root_nz,sizeof(struct cell*));
	}
	struct Particle p = r->particles[pt];
	int rootbox = particles_get_rootbox_for_particle(r, p);
#ifdef MPI
	// Do not add particles that do not belong to this tree (avoid removing active particles)
	int root_n_per_node = root_n/mpi_num;
	int proc_id = rootbox/root_n_per_node;
	if (proc_id!=mpi_id) return;
#endif 	// MPI
	r->tree_root[rootbox] = tree_add_particle_to_cell(r, r->tree_root[rootbox],pt,NULL,0);
}

struct cell *tree_add_particle_to_cell(struct Rebound* const r, struct cell *node, int pt, struct cell *parent, int o){
	struct Particle* const particles = r->particles;
	// Initialize a new node
	if (node == NULL) {  
		node = calloc(1, sizeof(struct cell));
		struct Particle p = particles[pt];
		if (parent == NULL){ // The new node is a root
			node->w = r->boxsize;
			int i = ((int)floor((p.x + r->boxsize_x/2.)/r->boxsize))%r->root_nx;
			int j = ((int)floor((p.y + r->boxsize_y/2.)/r->boxsize))%r->root_ny;
			int k = ((int)floor((p.z + r->boxsize_z/2.)/r->boxsize))%r->root_nz;
			node->x = -r->boxsize_x/2.+r->boxsize*(0.5+(double)i);
			node->y = -r->boxsize_y/2.+r->boxsize*(0.5+(double)j);
			node->z = -r->boxsize_z/2.+r->boxsize*(0.5+(double)k);
		}else{ // The new node is a normal node
			node->w 	= parent->w/2.;
			node->x 	= parent->x + node->w/2.*((o>>0)%2==0?1.:-1);
			node->y 	= parent->y + node->w/2.*((o>>1)%2==0?1.:-1);
			node->z 	= parent->z + node->w/2.*((o>>2)%2==0?1.:-1);
		}
		node->pt = pt; 
		particles[pt].c = node;
		for (int i=0; i<8; i++){
			node->oct[i] = NULL;
		}
		return node;
	}
	// In a existing node
	if (node->pt >= 0) { // It's a leaf node
		int o = tree_get_octant_for_particle_in_cell(particles[node->pt], node);
		node->oct[o] = tree_add_particle_to_cell(r, node->oct[o], node->pt, node, o); 
		o = tree_get_octant_for_particle_in_cell(particles[pt], node);
		node->oct[o] = tree_add_particle_to_cell(r, node->oct[o], pt, node, o);
		node->pt = -2;
	}else{ // It's not a leaf
		node->pt--;
		int o = tree_get_octant_for_particle_in_cell(particles[pt], node);
		node->oct[o] = tree_add_particle_to_cell(r, node->oct[o], pt, node, o);
	}
	return node;
}

int tree_get_octant_for_particle_in_cell(const struct Particle p, struct cell *node){
	int octant = 0;
	if (p.x < node->x) octant+=1;
	if (p.y < node->y) octant+=2;
	if (p.z < node->z) octant+=4;
	return octant;
}

/**
  * The function tests whether the particle is still within the cubic cell box. If the particle has moved outside the box, it returns 0. Otherwise, it returns 1. 
  *
  * @param node is the pointer to a node cell
  */
int tree_particle_is_inside_cell(const struct Rebound* const r, struct cell *node){
	if (fabs(r->particles[node->pt].x-node->x) > node->w/2. || \
		fabs(r->particles[node->pt].y-node->y) > node->w/2. || \
		fabs(r->particles[node->pt].z-node->z) > node->w/2.) {
		return 0;
	}
	return 1;
}

/**
  * The function is called to walk through the whole tree to update its structure and node->pt at the end of each time step.
  *
  * @param node is the pointer to a node cell
  */
struct cell *tree_update_cell(struct Rebound* const r, struct cell *node){
	int test = -1; /**< A temporary int variable is used to store the index of an octant when it needs to be freed. */
	if (node == NULL) {
		return NULL;
	}
	// Non-leaf nodes	
	if (node->pt < 0) {
		for (int o=0; o<8; o++) {
			node->oct[o] = tree_update_cell(r, node->oct[o]);
		}
		node->pt = 0;
		for (int o=0; o<8; o++) {
			struct cell *d = node->oct[o];
			if (d != NULL) {
				// Update node->pt
				if (d->pt >= 0) {	// The child is a leaf
					node->pt--;
					test = o;
				}else{				// The child cell contains several particles
					node->pt += d->pt;
				}
			}		
		}
		// Check if the node requires derefinement.
		if (node->pt == 0) {	// The node is empty.
			free(node);
			return NULL;
		} else if (node->pt == -1) { // The node becomes a leaf.
			node->pt = node->oct[test]->pt;
			r->particles[node->pt].c = node;
			free(node->oct[test]);
			node->oct[test]=NULL;
			return node;
		}
		return node;
	} 
	// Leaf nodes
	if (tree_particle_is_inside_cell(r, node) == 0) {
		int oldpos = node->pt;
		struct Particle reinsertme = r->particles[oldpos];
		if (oldpos<r->N_tree_fixed){
			particles_add_fixed(r,reinsertme,oldpos);
		}else{
			(r->N)--;
			r->particles[oldpos] = r->particles[r->N];
			r->particles[oldpos].c->pt = oldpos;
			particles_add(r, reinsertme);
		}
		free(node);
		return NULL; 
	} else {
		r->particles[node->pt].c = node;
		return node;
	}
}

/**
  * The function calculates the total mass and center of mass of a node. When QUADRUPOLE is defined, it also calculates the mass quadrupole tensor for all non-leaf nodes.
  */
void tree_update_gravity_data_in_cell(const struct Rebound* const r, struct cell *node){
#ifdef QUADRUPOLE
	node->mxx = 0;
	node->mxy = 0;
	node->mxz = 0;
	node->myy = 0;
	node->myz = 0;
	node->mzz = 0;
#endif // QUADRUPOLE
	if (node->pt < 0) {
		// Non-leaf nodes	
		node->m  = 0;
		node->mx = 0;
		node->my = 0;
		node->mz = 0;
		for (int o=0; o<8; o++) {
			struct cell* d = node->oct[o];
			if (d!=NULL){
				tree_update_gravity_data_in_cell(r, d);
				// Calculate the total mass and the center of mass
				double d_m = d->m;
				node->mx += d->mx*d_m;
				node->my += d->my*d_m;
				node->mz += d->mz*d_m;
				node->m  += d_m;
			}
		}
		double m_tot = node->m;
		if (m_tot>0){
			node->mx /= m_tot;
			node->my /= m_tot;
			node->mz /= m_tot;
		}
#ifdef QUADRUPOLE
		for (int o=0; o<8; o++) {
			struct cell* d = node->oct[o];
			if (d!=NULL){
				// Ref: Hernquist, L., 1987, APJS
				double d_m = d->m;
				double qx  = d->mx - node->mx;
				double qy  = d->my - node->my;
				double qz  = d->mz - node->mz;
				double qr2 = qx*qx + qy*qy + qz*qz;
				node->mxx += d->mxx + d_m*(3.*qx*qx - qr2);
				node->mxy += d->mxy + d_m*3.*qx*qy;
				node->mxz += d->mxz + d_m*3.*qx*qz;
				node->myy += d->myy + d_m*(3.*qy*qy - qr2);
				node->myz += d->myz + d_m*3.*qy*qz;
			}
		}
		node->mzz = -node->mxx -node->myy;
#endif // QUADRUPOLE
	}else{ 
		// Leaf nodes
		struct Particle p = r->particles[node->pt];
		node->m = p.m;
		node->mx = p.x;
		node->my = p.y;
		node->mz = p.z;
	}
}

void tree_update_gravity_data(struct Rebound* const r){
	for(int i=0;i<r->root_n;i++){
#ifdef MPI
		if (communication_mpi_rootbox_is_local(i)==1){
#endif // MPI
			if (r->tree_root[i]!=NULL){
				tree_update_gravity_data_in_cell(r, r->tree_root[i]);
			}
#ifdef MPI
		}
#endif // MPI
	}
}

void tree_update(struct Rebound* const r){
	if (r->tree_root==NULL){
		r->tree_root = calloc(r->root_nx*r->root_ny*r->root_nz,sizeof(struct cell*));
	}
	for(int i=0;i<r->root_n;i++){

#ifdef MPI
		if (communication_mpi_rootbox_is_local(i)==1){
#endif // MPI
			r->tree_root[i] = tree_update_cell(r, r->tree_root[i]);
#ifdef MPI
		}
#endif // MPI
	}
}



#ifdef MPI
/**
  * The function returns the index of the root which contains the cell.
  *
  * @param node is a pointer to a node cell.
  */
int particles_get_rootbox_for_node(struct cell* node){
	int i = ((int)floor((node->x + boxsize_x/2.)/boxsize)+root_nx)%root_nx;
	int j = ((int)floor((node->y + boxsize_y/2.)/boxsize)+root_ny)%root_ny;
	int k = ((int)floor((node->z + boxsize_z/2.)/boxsize)+root_nz)%root_nz;
	int index = (k*root_ny+j)*root_nx+i;
	return index;
}

/**
  * The function returns the octant index of a child cell within a parent cell.
  *
  * @param nnode is a pointer to a child cell of the cell which node points to.
  * @param node is a pointer to a node cell.
  */
int tree_get_octant_for_cell_in_cell(struct cell* nnode, struct cell *node){
	int octant = 0;
	if (nnode->x < node->x) octant+=1;
	if (nnode->y < node->y) octant+=2;
	if (nnode->z < node->z) octant+=4;
	return octant;
}

/**
  * Needs more comments!
  *
  * @param nnode is a pointer to a child cell of the cell which node points to.
  * @param node is a pointer to a node cell.
  */
void tree_add_essential_node_to_node(struct cell* nnode, struct cell* node){
	int o = tree_get_octant_for_cell_in_cell(nnode, node);
	if (node->oct[o]==NULL){
		node->oct[o] = nnode;
	}else{
		tree_add_essential_node_to_node(nnode, node->oct[o]);
	}
}

void tree_add_essential_node(struct cell* node){
	// Add essential node to appropriate parent.
	for (int o=0;o<8;o++){
		node->oct[o] = NULL;	
	}
	int index = particles_get_rootbox_for_node(node);
	if (r->tree_root[index]==NULL){
		r->tree_root[index] = node;
	}else{
		tree_add_essential_node_to_node(node, r->tree_root[index]);
	}
}
void tree_prepare_essential_tree_for_gravity(void){
	for(int i=0;i<root_n;i++){
		if (communication_mpi_rootbox_is_local(i)==1){
			communication_mpi_prepare_essential_tree_for_gravity(r->tree_root[i]);
		}else{
			// Delete essential tree reference. 
			// Tree itself is saved in tree_essential_recv[][] and
			// will be overwritten the next timestep.
			r->tree_root[i] = NULL;
		}
	}
}
void tree_prepare_essential_tree_for_collisions(void){
	for(int i=0;i<root_n;i++){
		if (communication_mpi_rootbox_is_local(i)==1){
			communication_mpi_prepare_essential_tree_for_collisions(r->tree_root[i]);
		}else{
			// Delete essential tree reference. 
			// Tree itself is saved in tree_essential_recv[][] and
			// will be overwritten the next timestep.
			r->tree_root[i] = NULL;
		}
	}
}
#endif // MPI

