//------------------------------------------------------------------------------------------------------------------------------
// Samuel Williams
// SWWilliams@lbl.gov
// Lawrence Berkeley National Lab
//------------------------------------------------------------------------------------------------------------------------------
#include <stdint.h>
#include "../timer.h"
//------------------------------------------------------------------------------------------------------------------------------
// Based on Yousef Saad's Iterative Methods for Sparse Linear Algebra, Algorithm 12.1, page 399
//------------------------------------------------------------------------------------------------------------------------------
#define DEGREE 16
//------------------------------------------------------------------------------------------------------------------------------
void smooth(domain_type * domain, int level, int x_id, int rhs_id, double a, double b){
  if( (domain->dominant_eigenvalue_of_DinvA[level]<=0.0) && (domain->rank==0) )printf("dominant_eigenvalue_of_DinvA[%d] <= 0.0 !\n",level);
  if(numSmooths&1){
    printf("error - numSmooths must be even...\n");
    exit(0);
  }

  int CollaborativeThreadingBoxSize = 100000; // i.e. never
  #ifdef __COLLABORATIVE_THREADING
    CollaborativeThreadingBoxSize = 1 << __COLLABORATIVE_THREADING;
  #endif
  int omp_across_boxes = (domain->subdomains[0].levels[level].dim.i <  CollaborativeThreadingBoxSize);
  int omp_within_a_box = (domain->subdomains[0].levels[level].dim.i >= CollaborativeThreadingBoxSize);

  int box,s;
  int ghosts = domain->ghosts;


  // compute the Chebyshev coefficients...
  double beta     = 1.100*1.000*domain->dominant_eigenvalue_of_DinvA[level];
  double alpha    = 0.900*0.333*domain->dominant_eigenvalue_of_DinvA[level];
  double theta    = 0.5*(beta+alpha); // center of the spectral ellipse
  double delta    = 0.5*(beta-alpha); // major axis?
  double sigma = theta/delta;
  double rho_n = 1/sigma; // rho_0
  double chebyshev_c1[DEGREE]; // + c1*(x_n-x_nm1) == rho_n*rho_nm1
  double chebyshev_c2[DEGREE]; // + c2*(b-Ax_n)
  chebyshev_c1[0] = 0.0;
  chebyshev_c2[0] = 1/theta;
  for(s=1;s<DEGREE;s++){
    double rho_nm1 = rho_n;
    rho_n = 1.0/(2.0*sigma - rho_nm1);
    chebyshev_c1[s] = rho_n*rho_nm1;
    chebyshev_c2[s] = rho_n*2.0/delta;
  }


  // if communication-avoiding, need RHS for stencils in ghost zones
  if(ghosts>1)exchange_boundary(domain,level,rhs_id,1,1,1);

  for(s=0;s<numSmooths;s+=ghosts){
    // Chebyshev ping pongs between phi and __temp
    if((s&1)==0)exchange_boundary(domain,level,  x_id,1,ghosts>1,ghosts>1);  // corners/edges if doing communication-avoiding...
           else exchange_boundary(domain,level,__temp,1,ghosts>1,ghosts>1);  // corners/edges if doing communication-avoiding...
    
    // now do ghosts communication-avoiding smooths on each box...
    uint64_t _timeStart = CycleTime();
    #pragma omp parallel for private(box) if(omp_across_boxes)
    for(box=0;box<domain->subdomains_per_rank;box++){
      int i,j,k,ss;
      int pencil = domain->subdomains[box].levels[level].pencil;
      int  plane = domain->subdomains[box].levels[level].plane;
      int ghosts = domain->subdomains[box].levels[level].ghosts;
      int  dim_k = domain->subdomains[box].levels[level].dim.k;
      int  dim_j = domain->subdomains[box].levels[level].dim.j;
      int  dim_i = domain->subdomains[box].levels[level].dim.i;
      double h2inv = 1.0/(domain->h[level]*domain->h[level]);
      //double * __restrict__ x_n       = domain->subdomains[box].levels[level].grids[    x_id] + ghosts*(1+pencil+plane); // i.e. [0] = first non ghost zone point
      //double * __restrict__ x_temp    = domain->subdomains[box].levels[level].grids[__temp  ] + ghosts*(1+pencil+plane); // x_nm1 is aliased to x_np1
      double * __restrict__ rhs       = domain->subdomains[box].levels[level].grids[  rhs_id] + ghosts*(1+pencil+plane);
      double * __restrict__ alpha     = domain->subdomains[box].levels[level].grids[__alpha ] + ghosts*(1+pencil+plane);
      double * __restrict__ beta_i    = domain->subdomains[box].levels[level].grids[__beta_i] + ghosts*(1+pencil+plane);
      double * __restrict__ beta_j    = domain->subdomains[box].levels[level].grids[__beta_j] + ghosts*(1+pencil+plane);
      double * __restrict__ beta_k    = domain->subdomains[box].levels[level].grids[__beta_k] + ghosts*(1+pencil+plane);
      double * __restrict__ lambda    = domain->subdomains[box].levels[level].grids[__lambda] + ghosts*(1+pencil+plane);

      int ghostsToOperateOn=ghosts-1;
      for(ss=s;ss<s+ghosts;ss++,ghostsToOperateOn--){
        double * __restrict__ x_np1;
        double * __restrict__ x_n;
        double * __restrict__ x_nm1;
              if((ss&1)==0){x_n    = domain->subdomains[box].levels[level].grids[    x_id] + ghosts*(1+pencil+plane);
                            x_nm1  = domain->subdomains[box].levels[level].grids[  __temp] + ghosts*(1+pencil+plane); 
                            x_np1  = domain->subdomains[box].levels[level].grids[  __temp] + ghosts*(1+pencil+plane);}
                       else{x_n    = domain->subdomains[box].levels[level].grids[  __temp] + ghosts*(1+pencil+plane);
                            x_nm1  = domain->subdomains[box].levels[level].grids[    x_id] + ghosts*(1+pencil+plane); 
                            x_np1  = domain->subdomains[box].levels[level].grids[    x_id] + ghosts*(1+pencil+plane);}
        double c1 = chebyshev_c1[ss%DEGREE]; // limit polynomial to degree DEGREE.
        double c2 = chebyshev_c2[ss%DEGREE]; // limit polynomial to degree DEGREE.
        #pragma omp parallel for private(k,j,i) if(omp_within_a_box) collapse(2)
        for(k=0-ghostsToOperateOn;k<dim_k+ghostsToOperateOn;k++){
        for(j=0-ghostsToOperateOn;j<dim_j+ghostsToOperateOn;j++){
        for(i=0-ghostsToOperateOn;i<dim_i+ghostsToOperateOn;i++){
          int ijk = i + j*pencil + k*plane;
          double Ax_n =  a*alpha[ijk]*x_n[ijk]
                             -b*h2inv*(
                                beta_i[ijk+1     ]*( x_n[ijk+1     ]-x_n[ijk       ] )
                               -beta_i[ijk       ]*( x_n[ijk       ]-x_n[ijk-1     ] )
                               +beta_j[ijk+pencil]*( x_n[ijk+pencil]-x_n[ijk       ] )
                               -beta_j[ijk       ]*( x_n[ijk       ]-x_n[ijk-pencil] )
                               +beta_k[ijk+plane ]*( x_n[ijk+plane ]-x_n[ijk       ] )
                               -beta_k[ijk       ]*( x_n[ijk       ]-x_n[ijk-plane ] )
                              );
          // According to Saad... missing a lambda[ijk] == D^{-1} !!!
          // x_{n+1} = x_{n} + rho_{n} [ rho_{n-1}(x_{n} - x_{n-1}) + (2/delta)(b-Ax_{n}) ]
          x_np1[ijk] = x_n[ijk] + c1*(x_n[ijk]-x_nm1[ijk]) + c2*lambda[ijk]*(rhs[ijk]-Ax_n);
          //x_temp[ijk] = x_n[ijk] + c1*(x_n[ijk]-x_temp[ijk]) + c2*lambda[ijk]*(rhs[ijk]-Ax_n);
        }}}
      } // ss-loop
    } // box-loop
    domain->cycles.smooth[level] += (uint64_t)(CycleTime()-_timeStart);
  } // s-loop
}
  
//------------------------------------------------------------------------------------------------------------------------------
/*
    #pragma omp parallel for private(box) if(omp_across_boxes)
    for(box=0;box<domain->subdomains_per_rank;box++){
      int i,j,k,ss;
      int pencil = domain->subdomains[box].levels[level].pencil;
      int  plane = domain->subdomains[box].levels[level].plane;
      int ghosts = domain->subdomains[box].levels[level].ghosts;
      int  dim_k = domain->subdomains[box].levels[level].dim.k;
      int  dim_j = domain->subdomains[box].levels[level].dim.j;
      int  dim_i = domain->subdomains[box].levels[level].dim.i;
      double h2inv = 1.0/(domain->h[level]*domain->h[level]);
      double * __restrict__ x_k       = domain->subdomains[box].levels[level].grids[    x_id] + ghosts*(1+pencil+plane); // i.e. [0] = first non ghost zone point
      double * __restrict__ x_temp    = domain->subdomains[box].levels[level].grids[__temp  ] + ghosts*(1+pencil+plane); // x_km1 is aliased to x_kp1
      double * __restrict__ rhs       = domain->subdomains[box].levels[level].grids[  rhs_id] + ghosts*(1+pencil+plane);
      double * __restrict__ alpha     = domain->subdomains[box].levels[level].grids[__alpha ] + ghosts*(1+pencil+plane);
      double * __restrict__ beta_i    = domain->subdomains[box].levels[level].grids[__beta_i] + ghosts*(1+pencil+plane);
      double * __restrict__ beta_j    = domain->subdomains[box].levels[level].grids[__beta_j] + ghosts*(1+pencil+plane);
      double * __restrict__ beta_k    = domain->subdomains[box].levels[level].grids[__beta_k] + ghosts*(1+pencil+plane);
      double * __restrict__ lambda    = domain->subdomains[box].levels[level].grids[__lambda] + ghosts*(1+pencil+plane);

      int ghostsToOperateOn=ghosts-1;
      for(ss=0;ss<ghosts;ss++,ghostsToOperateOn--){
        double c1 = chebyshev_c1[(sweep+ss)%DEGREE]; // limit polynomial to degree DEGREE.
        double c2 = chebyshev_c2[(sweep+ss)%DEGREE]; // limit polynomial to degree DEGREE.
        #pragma omp parallel for private(k,j,i) if(omp_within_a_box) collapse(2)
        for(k=0-ghostsToOperateOn;k<dim_k+ghostsToOperateOn;k++){
        for(j=0-ghostsToOperateOn;j<dim_j+ghostsToOperateOn;j++){
        for(i=0-ghostsToOperateOn;i<dim_i+ghostsToOperateOn;i++){
          int ijk = i + j*pencil + k*plane;
          double Ax_k =  a*alpha[ijk]*x_k[ijk]
                             -b*h2inv*(
                                beta_i[ijk+1     ]*( x_k[ijk+1     ]-x_k[ijk       ] )
                               -beta_i[ijk       ]*( x_k[ijk       ]-x_k[ijk-1     ] )
                               +beta_j[ijk+pencil]*( x_k[ijk+pencil]-x_k[ijk       ] )
                               -beta_j[ijk       ]*( x_k[ijk       ]-x_k[ijk-pencil] )
                               +beta_k[ijk+plane ]*( x_k[ijk+plane ]-x_k[ijk       ] )
                               -beta_k[ijk       ]*( x_k[ijk       ]-x_k[ijk-plane ] )
                              );
          // x_{k+1} = x_{k} + rho_{k} [ rho_{k-1}(x_{k} - x_{k-1}) + (2/delta)(b-Ax_{k}) ]    !!! version in Saad's book is missing a lambda[ijk] == D^{-1} !!!
          //x_kp1[ijk] = x_k[ijk] + c1*(x_k[ijk]-x_km1[ijk]) + c2*lambda[ijk]*(rhs[ijk]-Ax_k);
          x_temp[ijk] = x_k[ijk] + c1*(x_k[ijk]-x_temp[ijk]) + c2*lambda[ijk]*(rhs[ijk]-Ax_k);
        }}}
        #pragma omp parallel for private(k,j,i) if(omp_within_a_box) collapse(2)
        for(k=0-ghostsToOperateOn;k<dim_k+ghostsToOperateOn;k++){
        for(j=0-ghostsToOperateOn;j<dim_j+ghostsToOperateOn;j++){
        for(i=0-ghostsToOperateOn;i<dim_i+ghostsToOperateOn;i++){
          // rotate x_kp1[], x_k, and x_km1[]
          int ijk = i + j*pencil + k*plane;
          double _x_kp1 = x_temp[ijk]; // save x_kp1
            x_temp[ijk] =    x_k[ijk]; // x_km1 = x_k
               x_k[ijk] =      _x_kp1; // x_k   = x_kp1
        }}}
      }
    }
    domain->cycles.smooth[level] += (uint64_t)(CycleTime()-_timeStart);
  }
*/
