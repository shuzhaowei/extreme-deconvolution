/*
  NAME:
     proj_EM_step
  PURPOSE:
     one proj_EM step
  CALLING SEQUENCE:
     proj_EM_step(struct datapoint * data, int N, struct gaussian * gaussians,
     int K,bool * fixamp, bool * fixmean, bool * fixcovar, 
     double * avgloglikedata, bool likeonly, double w, bool noproj, 
     bool diagerrs, bool noweight)
  INPUT:
     data         - the data
     N            - number of data points
     gaussians    - model gaussians
     K            - number of model gaussians
     fixamp       - fix the amplitude?
     fixmean      - fix the mean?
     fixcovar     - fix the covar?
     likeonly     - only compute likelihood?
     w            - regularization parameter
     noproj       - don't perform any projections
     diagerrs     - the data->SS errors-squared are diagonal
     noweight     - don't use data-weights
  OUTPUT:
     avgloglikedata - average loglikelihood of the data
  REVISION HISTORY:
     2008-09-21 - Written Bovy
     2010-03-01 Added noproj option - Bovy
     2010-04-01 Added noweight option - Bovy
*/
#include <omp.h>
#include <math.h>
#include <float.h>
#include <gsl/gsl_matrix.h>
#include <gsl/gsl_vector.h>
#include <gsl/gsl_linalg.h>
#include <gsl/gsl_blas.h>
#include <proj_gauss_mixtures.h>

#define CHUNKSIZE 100

void proj_EM_step(struct datapoint * data, int N, 
		  struct gaussian * gaussians, int K,bool * fixamp, 
		  bool * fixmean, bool * fixcovar, double * avgloglikedata, 
		  bool likeonly, double w, bool noproj, bool diagerrs,
		  bool noweight){
  *avgloglikedata = 0.0;
  
  struct datapoint * thisdata;
  struct gaussian * thisgaussian;
  int signum,di;
  double exponent;
  double currqij;
  struct gaussian * startgaussians = gaussians;
  struct modelbs * thisbs;
  int d = (gaussians->VV)->size1;//dim of mm

  //Initialize new parameters
  int kk;
  for (kk=0; kk != K; ++kk){
    newgaussians->alpha = 0.0;
    gsl_vector_set_zero(newgaussians->mm);
    gsl_matrix_set_zero(newgaussians->VV);
    ++newgaussians;
  }
  newgaussians= startnewgaussians;
    
  
  //check whether for some Gaussians none of the parameters get updated
  double sumfixedamps= 0;
  bool * allfixed = (bool *) calloc(K, sizeof (bool) );
  double ampnorm;
  for (kk=0; kk != K; ++kk){
    if (*fixamp == true){
      sumfixedamps += gaussians->alpha;
    }
    ++gaussians;
    if (*fixamp == true && *fixmean == true && *fixcovar == true)
      *allfixed= true;
    ++allfixed;
    ++fixamp;
    ++fixmean;
    ++fixcovar;
  }
  gaussians -= K;
  allfixed -= K;
  fixamp -= K;
  fixmean -= K;
  fixcovar -= K;


  //now loop over data and gaussians to update the model parameters
  int ii, jj, ll;
  double sumSV;
  int chunk;
  chunk= CHUNKSIZE;
#pragma omp parallel for ordered schedule(dynamic,chunk) private(di,signum,exponent,ii,jj,Tij,Tij_inv,wminusRm,p,VRTTinv,sumSV,VRT,TinvwminusRm,Rtrans,thisgaussian,thisdata) shared(newgaussians,gaussians,bs)
  for (ii = 0 ; ii < N; ++ii){
    thisdata= data+ii;
    tid= omp_get_thread_num();
    for (jj = 0; jj != K; ++jj){
      //printf("%i\n",omp_get_thread_num());
      //printf("%i\n",omp_get_num_threads());
      thisgaussian= gaussians+jj;
      //prepare...
      di = (thisdata->SS)->size1;
      //printf("Datapoint has dimension %i\n",di);
      p = gsl_permutation_alloc (di);
      wminusRm = gsl_vector_alloc (di);
      gsl_vector_memcpy(wminusRm,thisdata->ww);
      TinvwminusRm = gsl_vector_alloc (di);
      Tij = gsl_matrix_alloc(di,di);
      if ( ! noproj ) {
	if ( diagerrs ) {
	  gsl_matrix_set_zero(Tij);
	  for (ll = 0; ll != di; ++ll)
	    gsl_matrix_set(Tij,ll,ll,gsl_matrix_get(thisdata->SS,ll,0));}
	else
	  gsl_matrix_memcpy(Tij,thisdata->SS);
      }
      Tij_inv = gsl_matrix_alloc(di,di);
      if ( ! noproj ) VRT = gsl_matrix_alloc(d,di);
      VRTTinv = gsl_matrix_alloc(d,di);
      if ( ! noproj ) Rtrans = gsl_matrix_alloc(d,di);
      //Calculate Tij
      if ( ! noproj ) {
	gsl_matrix_transpose_memcpy(Rtrans,thisdata->RR);
	gsl_blas_dsymm(CblasLeft,CblasUpper,1.0,thisgaussian->VV,Rtrans,0.0,VRT);//Only the upper right part of VV is calculated --> use only that part
	gsl_blas_dgemm(CblasNoTrans,CblasNoTrans,1.0,thisdata->RR,VRT,1.0,Tij);}//This is Tij
      else {
	if ( diagerrs ) {
	  for (kk = 0; kk != d; ++kk){
	    gsl_matrix_set(Tij,kk,kk,gsl_matrix_get(thisdata->SS,kk,0)+gsl_matrix_get(thisgaussian->VV,kk,kk));
	    for (ll = kk+1; ll != d; ++ll){
	      sumSV= gsl_matrix_get(thisgaussian->VV,kk,ll);
	      gsl_matrix_set(Tij,kk,ll,sumSV);
	      gsl_matrix_set(Tij,ll,kk,sumSV);}}}
	else {
	  for (kk = 0; kk != d; ++kk){
	    gsl_matrix_set(Tij,kk,kk,gsl_matrix_get(thisdata->SS,kk,kk)+gsl_matrix_get(thisgaussian->VV,kk,kk));
	    for (ll = kk+1; ll != d; ++ll){
	      sumSV= gsl_matrix_get(thisdata->SS,kk,ll)+gsl_matrix_get(thisgaussian->VV,kk,ll);
	      gsl_matrix_set(Tij,kk,ll,sumSV);
	      gsl_matrix_set(Tij,ll,kk,sumSV);}}}}
      //gsl_matrix_add(Tij,thisgaussian->VV);}
      //Calculate LU decomp of Tij and Tij inverse
      gsl_linalg_LU_decomp(Tij,p,&signum);
      gsl_linalg_LU_invert(Tij,p,Tij_inv);
      //Calculate Tijinv*(w-Rm)
      if ( ! noproj ) gsl_blas_dgemv(CblasNoTrans,-1.0,thisdata->RR,thisgaussian->mm,1.0,wminusRm);
      else gsl_vector_sub(wminusRm,thisgaussian->mm);
      //printf("wminusRm = %f\t%f\n",gsl_vector_get(wminusRm,0),gsl_vector_get(wminusRm,1));
      gsl_blas_dsymv(CblasUpper,1.0,Tij_inv,wminusRm,0.0,TinvwminusRm);
      //printf("TinvwminusRm = %f\t%f\n",gsl_vector_get(TinvwminusRm,0),gsl_vector_get(TinvwminusRm,1));
      gsl_blas_ddot(wminusRm,TinvwminusRm,&exponent);
      //printf("Exponent = %f\nDet = %f\n",exponent,gsl_linalg_LU_det(Tij,signum));
      gsl_matrix_set(qij,ii,jj,log(thisgaussian->alpha) - di * halflogtwopi - 0.5 * gsl_linalg_LU_lndet(Tij) -0.5 * exponent);//This is actually the log of qij
      //printf("Here we have = %f\n",gsl_matrix_get(qij,ii,jj));
      //Now calculate bij and Bij
      thisbs= bs+tid*K+jj;
      gsl_vector_memcpy(thisbs->bbij,thisgaussian->mm);
      //fprintf(stdout,"Where is the seg fault?\n");
      if ( ! noproj ) gsl_blas_dgemv(CblasNoTrans,1.0,VRT,TinvwminusRm,1.0,thisbs->bbij);
      else gsl_blas_dsymv(CblasUpper,1.0,thisgaussian->VV,TinvwminusRm,1.0,thisbs->bbij);
      //printf("bij = %f\t%f\n",gsl_vector_get(bs->bbij,0),gsl_vector_get(bs->bbij,1));
      gsl_matrix_memcpy(thisbs->BBij,thisgaussian->VV);
      if ( ! noproj ) {
	gsl_blas_dgemm(CblasNoTrans,CblasNoTrans,1.0,VRT,Tij_inv,0.0,VRTTinv);
	gsl_blas_dgemm(CblasNoTrans,CblasTrans,-1.0,VRTTinv,VRT,1.0,thisbs->BBij);}
      else {
	gsl_blas_dsymm(CblasLeft,CblasUpper,1.0,thisgaussian->VV,Tij_inv,0.0,VRTTinv);
	gsl_blas_dsymm(CblasRight,CblasUpper,-1.0,thisgaussian->VV,VRTTinv,1.0,thisbs->BBij);}
      gsl_blas_dsyr(CblasUpper,1.0,thisbs->bbij,thisbs->BBij);//This is bijbijT + Bij, which is the relevant quantity
      //Clean up
      gsl_permutation_free (p);
      gsl_vector_free(wminusRm);
      gsl_vector_free(TinvwminusRm);
      gsl_matrix_free(Tij);
      gsl_matrix_free(Tij_inv);
      if ( ! noproj ) gsl_matrix_free(VRT);
      gsl_matrix_free(VRTTinv);
      if ( ! noproj ) gsl_matrix_free(Rtrans);
      //++gaussians;
      //++bs;
    }
    //bs -= K;
    //gaussians = startgaussians;
    //Normalize qij properly
    *avgloglikedata += normalize_row(qij,ii,true,noweight,thisdata->logweight);
    //if (likeonly){
    //  ++data;
    //  continue;
    //}
    //printf("qij = %f\t%f\n",gsl_matrix_get(qij,ii,0),gsl_matrix_get(qij,ii,1));
    //printf("avgloglgge = %f\n",*avgloglikedata);
    //Again loop over the gaussians to update the model(can this be more efficient? in any case this is not so bad since generally K << N)
#pragma omp ordered
    {
      for (jj = 0; jj != K; ++jj){
	if (*(allfixed++)){
	  ++newgaussians;
	  //++bs;
	  continue;
	}
	else {
	  currqij = exp(gsl_matrix_get(qij,ii,jj));
	  //printf("Current qij = %f\n",currqij);
	  thisbs= bs+tid*K+jj;
	  gsl_vector_scale(thisbs->bbij,currqij);
	  gsl_vector_add(newgaussians->mm,thisbs->bbij);
	  gsl_matrix_scale(thisbs->BBij,currqij);
	  gsl_matrix_add(newgaussians->VV,thisbs->BBij);
	  //printf("bij = %f\t%f\n",gsl_vector_get(bs->bbij,0),gsl_vector_get(bs->bbij,1));
	  //printf("Bij = %f\t%f\t%f\n",gsl_matrix_get(bs->BBij,0,0),gsl_matrix_get(bs->BBij,1,1),gsl_matrix_get(bs->BBij,0,1));
	  ++newgaussians;
	  //++bs;
	}
      }
      //bs -= K;
      allfixed -= K;
      newgaussians = startnewgaussians;
      //++data;
    }
    //data -= N;
  }
  
  *avgloglikedata /= N;
  if (likeonly)
    return;

  //Now update the parameters
  //Thus, loop over gaussians again!
  double qj;
  for (jj = 0; jj != K; ++jj){
    if (*(allfixed++)){
      ++fixamp;
      ++fixmean;
      ++fixcovar;
      ++gaussians;
      ++newgaussians;
      continue;
    }
    else {
      qj = exp(logsum(qij,jj,false));
      (qj < DBL_MIN) ? qj = 0: 0;
      //printf("qj = %f\n",qj);
      if (*fixamp != true) {
	gaussians->alpha = qj;
	if (qj == 0) {//rethink this
	  *fixamp=1;
	  *fixmean=1;
	  *fixcovar=1;
	  ++fixamp;
	  ++fixmean;
	  ++fixcovar;
	  ++gaussians;
	  ++newgaussians;
	  continue;
	}
      }
    if (*fixmean != true){
      gsl_vector_scale(newgaussians->mm,1.0/qj);
      gsl_vector_memcpy(gaussians->mm,newgaussians->mm);
    }
    if (*fixcovar != true){
      if (*fixmean != true)
	gsl_blas_dsyr(CblasUpper,-qj,gaussians->mm,newgaussians->VV);
      else {
	gsl_blas_dsyr(CblasUpper,qj,gaussians->mm,newgaussians->VV);
	gsl_blas_dsyr2(CblasUpper,-qj,gaussians->mm,newgaussians->mm,newgaussians->VV);
      }
      if (w > 0.){
	gsl_matrix_add(newgaussians->VV,I);
	gsl_matrix_scale(newgaussians->VV,1.0/(qj+1.0));
      }
      else gsl_matrix_scale(newgaussians->VV,1.0/qj);
      gsl_matrix_memcpy(gaussians->VV,newgaussians->VV);
    }
    ++fixamp;
    ++fixmean;
    ++fixcovar;
    ++gaussians;
    ++newgaussians;
    }
  }
  newgaussians = startnewgaussians;
  gaussians= startgaussians;
  fixamp -= K;
  fixmean -= K;
  fixcovar -= K;
  allfixed -= K;

  //normalize the amplitudes
  if ( sumfixedamps == 0. && noweight ){
    for (kk=0; kk != K; ++kk){
      if ( noweight ) (gaussians++)->alpha /= (double) N;
    }
  }
  else {
    ampnorm= 0;
    for (kk=0; kk != K; ++kk){
      if (*(fixamp++) == false) ampnorm += gaussians->alpha;
      ++gaussians;
    }
    fixamp -= K;
    gaussians -= K;
    for (kk=0; kk != K; ++kk){
      if (*(fixamp++) == false){
	gaussians->alpha /= ampnorm;
	gaussians->alpha *= (1. - sumfixedamps);
      }
      ++gaussians;
    }
    fixamp -= K;
    gaussians -= K;
  }


  free(allfixed);


  return;
}

