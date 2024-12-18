/**************************************************************************************/
/**                                                                                \n**/
/**                u  p  d  a  t  e  _  d  a  i  l  y  .  c                        \n**/
/**                                                                                \n**/
/**     C implementation of LPJmL                                                  \n**/
/**                                                                                \n**/
/**     Function of daily update of individual grid cell                           \n**/
/**                                                                                \n**/
/** (C) Potsdam Institute for Climate Impact Research (PIK), see COPYRIGHT file    \n**/
/** authors, and contributors see AUTHORS file                                     \n**/
/** This file is part of LPJmL and licensed under GNU AGPL Version 3               \n**/
/** or later. See LICENSE file or go to http://www.gnu.org/licenses/               \n**/
/** Contact: https://github.com/PIK-LPJmL/LPJmL                                    \n**/
/**                                                                                \n**/
/**************************************************************************************/

#include "lpj.h"

#define length 1.0 /* characteristic length (m) */
#ifdef IMAGE
#define GWCOEFF 100 /**< groundwater outflow coefficient (average amount of release time in reservoir) */
#endif
#define BIOTURBRATE 0.001897 /* daily rate for 50% annual bioturbation rate [-]*/
#define LEAF 0
#define WOOD 1

void update_daily(Cell *cell,            /**< cell pointer           */
                  Real co2,              /**< atmospheric CO2 (ppmv) */
                  Real popdensity,       /**< population density (capita/km2) */
                  Dailyclimate climate,  /**< Daily climate values */
                  int day,               /**< day (1..365)           */
                  int npft,              /**< number of natural PFTs */
                  int ncft,              /**< number of crop PFTs   */
                  int year,              /**< simulation year */
                  int month,             /**< month (0..11) */
                  Bool intercrop,        /**< enable intercropping */
                  const Config *config   /**< LPJmL configuration */
                 )
{
  int s,p;
  Pft *pft;
  Real melt=0,eeq,par,daylength,beta;
  Real runoff,snowrunoff;
#ifdef IMAGE
  Real fout_gw; // local variable for groundwater outflow (baseflow)
#endif
  Real gtemp_air;  /* value of air temperature response function */
  Real gtemp_soil[NSOILLAYER]; /* value of soil temperature response function */
  Stocks flux_estab={0,0};
  Real evap=0;
  Stocks hetres={0,0};
  Real avgprec;
  Stand *stand;
  Real bnf;
  Real nh3;
  int l,i;
  Livefuel livefuel={0,0,0,0,0};
  const Real prec_save=climate.prec;
  Real agrfrac;
  Real litsum_old_nv[2]={0,0},litsum_new_nv[2]={0,0};
  Real litsum_old_agr[2]={0,0},litsum_new_agr[2]={0,0};
  Irrigation *data;

  updategdd(cell->gdd,config->pftpar,npft,climate.temp);
  cell->balance.aprec+=climate.prec;
  gtemp_air=temp_response(climate.temp);
  daily_climbuf(&cell->climbuf,climate.temp,climate.prec);
  avgprec=getavgprec(&cell->climbuf);
  getoutput(&cell->output,SNOWF,config)+=climate.temp<tsnow ? climate.prec : 0;
  getoutput(&cell->output,RAIN,config)+=climate.temp<tsnow ? 0 : climate.prec;

  if(config->withlanduse) /* landuse enabled? */
    flux_estab=sowing(cell,climate.prec,day,year,npft,ncft,config); 
  cell->discharge.drunoff=0.0;

  if(config->fire==SPITFIRE || config->fire==SPITFIRE_TMAX)
    update_nesterov(cell,&climate);

  agrfrac=0;
  foreachstand(stand,s,cell->standlist)
    if(isagriculture(stand->type->landusetype))
      agrfrac+=stand->frac;

  foreachstand(stand,s,cell->standlist)
  {
    for(l=0;l<stand->soil.litter.n;l++)
    {
      stand->soil.litter.item[l].agsub.leaf.carbon += stand->soil.litter.item[l].agtop.leaf.carbon*param.bioturbate;
      stand->soil.litter.item[l].agtop.leaf.carbon *= (1 - param.bioturbate);
      stand->soil.litter.item[l].agsub.leaf.nitrogen += stand->soil.litter.item[l].agtop.leaf.nitrogen*param.bioturbate;
      stand->soil.litter.item[l].agtop.leaf.nitrogen *= (1 - param.bioturbate);
    }

    beta=albedo_stand(stand);
    radiation(&daylength,&par,&eeq,cell->coord.lat,day,&climate,beta,config->with_radiation);
    getoutput(&cell->output,PET,config)+=eeq*PRIESTLEY_TAYLOR*stand->frac;
    cell->output.mpet+=eeq*PRIESTLEY_TAYLOR*stand->frac;
    getoutput(&cell->output,ALBEDO,config) += beta * stand->frac;

    if((config->fire==SPITFIRE  || config->fire==SPITFIRE_TMAX)&& cell->afire_frac<1)
      dailyfire_stand(stand,&livefuel,popdensity,avgprec,&climate,config);
    if(config->permafrost)
    {
      snowrunoff=snow(&stand->soil,&climate.prec,&melt,
                      climate.temp,&evap)*stand->frac;
      cell->discharge.drunoff+=snowrunoff;
      getoutput(&cell->output,EVAP,config)+=evap*stand->frac; /* evap from snow runoff*/
      cell->balance.aevap+=evap*stand->frac; /* evap from snow runoff*/
#if defined IMAGE && defined COUPLED
  if(cell->ml.image_data!=NULL)
    cell->ml.image_data->mevapotr[month] += evap*stand->frac;
#endif

#ifdef MICRO_HEATING
      /*THIS IS DEDICATED TO MICROBIOLOGICAL HEATING*/
      foreachsoillayer(l)
        stand->soil.micro_heating[l]=m_heat*stand->soil.decomC[l];
      stand->soil.micro_heating[0]+=m_heat*stand->soil.litter.decomC;
#endif

      update_soil_thermal_state(&stand->soil,climate.temp,config);
    }
    else
    {
      stand->soil.temp[0]=soiltemp_lag(&stand->soil,&cell->climbuf);
      for(l=1;l<NSOILLAYER;l++)
        stand->soil.temp[l]=stand->soil.temp[0];
      snowrunoff=snow_old(&stand->soil.snowpack,&climate.prec,&melt,climate.temp)*stand->frac;
      cell->discharge.drunoff+=snowrunoff;
    }

    foreachsoillayer(l)
      gtemp_soil[l]=temp_response(stand->soil.temp[l]);
    getoutput(&cell->output,SOILTEMP1,config)+=stand->soil.temp[0]*stand->frac*(1.0/(1-cell->lakefrac-cell->ml.reservoirfrac));
    getoutput(&cell->output,SOILTEMP2,config)+=stand->soil.temp[1]*stand->frac*(1.0/(1-cell->lakefrac-cell->ml.reservoirfrac));
    getoutput(&cell->output,SOILTEMP3,config)+=stand->soil.temp[2]*stand->frac*(1.0/(1-cell->lakefrac-cell->ml.reservoirfrac));
    getoutput(&cell->output,SOILTEMP4,config)+=stand->soil.temp[3]*stand->frac*(1.0/(1-cell->lakefrac-cell->ml.reservoirfrac));
    getoutput(&cell->output,SOILTEMP5,config)+=stand->soil.temp[4]*stand->frac*(1.0/(1-cell->lakefrac-cell->ml.reservoirfrac));
    getoutput(&cell->output,SOILTEMP6,config)+=stand->soil.temp[5]*stand->frac*(1.0/(1-cell->lakefrac-cell->ml.reservoirfrac));
    foreachsoillayer(l)
      getoutputindex(&cell->output,SOILTEMP,l,config)+=stand->soil.temp[l]*stand->frac*(1.0/(1-cell->lakefrac-cell->ml.reservoirfrac));
    getoutput(&cell->output,TWS,config)+=stand->soil.litter.agtop_moist*stand->frac;
    /* update soil and litter properties to account for all changes since last call of littersom */
    if(config->soilpar_option==NO_FIXED_SOILPAR || (config->soilpar_option==FIXED_SOILPAR && year<config->soilpar_fixyear))
      pedotransfer(stand,NULL,NULL,stand->frac);
    updatelitterproperties(stand,stand->frac);

    if(stand->type->landusetype==NATURAL)
      for(l=0;l<stand->soil.litter.n;l++)
      {
        litsum_old_nv[LEAF]+=stand->soil.litter.item[l].agtop.leaf.carbon+stand->soil.litter.item[l].agsub.leaf.carbon+stand->soil.litter.item[l].bg.carbon;
        for(i=0;i<NFUELCLASS;i++)
          litsum_old_nv[WOOD]+=stand->soil.litter.item[l].agtop.wood[i].carbon+stand->soil.litter.item[l].agsub.wood[i].carbon;
      }
    if(isagriculture(stand->type->landusetype))
      for(l=0;l<stand->soil.litter.n;l++)
      {
        litsum_old_agr[LEAF]+=stand->soil.litter.item[l].agtop.leaf.carbon+stand->soil.litter.item[l].agsub.leaf.carbon+stand->soil.litter.item[l].bg.carbon;
        for(i=0;i<NFUELCLASS;i++)
          litsum_old_agr[WOOD]+=stand->soil.litter.item[l].agtop.wood[i].carbon+stand->soil.litter.item[l].agsub.wood[i].carbon;
      }

    hetres=littersom(stand,gtemp_soil,agrfrac,npft,ncft,config);
    cell->balance.arh+=hetres.carbon*stand->frac;
    getoutput(&cell->output,RH,config)+=hetres.carbon*stand->frac;
    getoutput(&cell->output,N2O_NIT,config)+=hetres.nitrogen*stand->frac;
    cell->balance.n_outflux+=hetres.nitrogen*stand->frac;

    if(stand->type->landusetype==NATURAL)
      for(l=0;l<stand->soil.litter.n;l++)
      {
        litsum_new_nv[LEAF]+=stand->soil.litter.item[l].agtop.leaf.carbon+stand->soil.litter.item[l].agsub.leaf.carbon+stand->soil.litter.item[l].bg.carbon;
        for(i=0;i<NFUELCLASS;i++)
          litsum_new_nv[WOOD]+=stand->soil.litter.item[l].agtop.wood[i].carbon+stand->soil.litter.item[l].agsub.wood[i].carbon;
      }
    if(isagriculture(stand->type->landusetype))
      for(l=0;l<stand->soil.litter.n;l++)
      {
        litsum_new_agr[LEAF]+=stand->soil.litter.item[l].agtop.leaf.carbon+stand->soil.litter.item[l].agsub.leaf.carbon+stand->soil.litter.item[l].bg.carbon;
        for(i=0;i<NFUELCLASS;i++)
          litsum_new_agr[WOOD]+=stand->soil.litter.item[l].agtop.wood[i].carbon+stand->soil.litter.item[l].agsub.wood[i].carbon;
      }

    /* update soil and litter properties to account for all changes from littersom */
    if(config->soilpar_option==NO_FIXED_SOILPAR || (config->soilpar_option==FIXED_SOILPAR && year<config->soilpar_fixyear))
      pedotransfer(stand,NULL,NULL,stand->frac);
    updatelitterproperties(stand,stand->frac);

    /*monthly rh for agricutural stands*/
    if (isagriculture(stand->type->landusetype))
    {
      getoutput(&cell->output,RH_AGR,config)+=hetres.carbon*stand->frac/agrfrac;
      getoutput(&cell->output,N2O_NIT_AGR,config)+=hetres.nitrogen*stand->frac;
    }
    if(stand->type->landusetype==GRASSLAND)
    {
      getoutput(&cell->output,N2O_NIT_MGRASS,config)+=hetres.nitrogen*stand->frac;
      getoutput(&cell->output,RH_MGRASS,config)+=hetres.carbon*stand->frac;
    }
    cell->output.dcflux+=hetres.carbon*stand->frac;
#if defined IMAGE && defined COUPLED
    if (stand->type->landusetype == NATURAL)
    {
      cell->rh_nat += hetres.carbon*stand->frac;
    } /* if NATURAL */
    if (stand->type->landusetype == WOODPLANTATION)
    {
      cell->rh_wp += hetres.carbon*stand->frac;
    } /* if woodplantation */
#endif

    getoutput(&cell->output,LITTERTEMP,config)+=stand->soil.litter.agtop_temp*stand->frac*(1.0/(1-cell->lakefrac-cell->ml.reservoirfrac));
    getoutput(&cell->output,SWE,config)+=stand->soil.snowpack*stand->frac;
    getoutput(&cell->output,TWS,config)+=stand->soil.snowpack*stand->frac;
    getoutput(&cell->output,SNOWRUNOFF,config)+=snowrunoff;
    getoutput(&cell->output,MELT,config)+=melt*stand->frac;

    if(config->fire==FIRE && climate.temp>0)
      stand->fire_sum+=fire_sum(&stand->soil.litter,stand->soil.w[0]);

    if(config->with_nitrogen)
    {
      if(config->with_nitrogen==UNLIM_NITROGEN || 
         (config->equilsoil && param.veg_equil_unlim && year<=(config->firstyear-config->nspinup+param.veg_equil_year)))
      {
        if(stand->soil.par->type==ROCK)
        {
          getoutput(&cell->output,LEACHING,config)+=2000*stand->frac;
          cell->balance.n_outflux+=2000*stand->frac;
          if (isagriculture(stand->type->landusetype))
            getoutput(&cell->output,NLEACHING_AGR,config)+=2000*stand->frac;
        }
        else
        {
          stand->soil.NH4[0]+=1000;
          stand->soil.NO3[0]+=1000;
        }
        cell->balance.influx.nitrogen+=2000*stand->frac;
        if (isagriculture(stand->type->landusetype))
          getoutput(&cell->output,NDEPO_AGR,config)+=2000*stand->frac;
        if(stand->type->landusetype!=NATURAL && stand->type->landusetype!=WOODPLANTATION)
          getoutput(&cell->output,NDEPO_MG,config)+=2000*stand->frac;
        getoutput(&cell->output,NDEPOS,config)+=2000*stand->frac;
      }
      else if(!config->no_ndeposition)
      {
        if(stand->soil.par->type==ROCK)
        {
          getoutput(&cell->output,LEACHING,config)+=(climate.nh4deposition+climate.no3deposition)*stand->frac;
          cell->balance.n_outflux+=(climate.nh4deposition+climate.no3deposition)*stand->frac;
          if (isagriculture(stand->type->landusetype))
            getoutput(&cell->output,NLEACHING_AGR,config)+=(climate.nh4deposition+climate.no3deposition)*stand->frac;
        }
        else
        {
          /*adding daily N deposition to upper soil layer*/
          stand->soil.NH4[0]+=climate.nh4deposition;
          stand->soil.NO3[0]+=climate.no3deposition;
        }
        cell->balance.influx.nitrogen+=(climate.nh4deposition+climate.no3deposition)*stand->frac;
        if (isagriculture(stand->type->landusetype))
          getoutput(&cell->output,NDEPO_AGR,config)+=(climate.nh4deposition+climate.no3deposition)*stand->frac;
        if(stand->type->landusetype!=NATURAL && stand->type->landusetype!=WOODPLANTATION)
          getoutput(&cell->output,NDEPO_MG,config)+=(climate.nh4deposition+climate.no3deposition)*stand->frac;
        getoutput(&cell->output,NDEPOS,config)+=(climate.nh4deposition+climate.no3deposition)*stand->frac;
      }
#ifdef DEBUG_N
      printf("BEFORE_STRESS[%s], day %d: ",stand->type->name,day);
      for(l=0;l<NSOILLAYER-1;l++)
        printf("%g ",stand->soil.NO3[l]);
      printf("\n");
#endif
#ifdef DEBUG_N
      printf("AFTER_STRESS: ");
      for(l=0;l<NSOILLAYER-1;l++)
        printf("%g ",stand->soil.NO3[l]);
      printf("\n");
#endif

    } /* of if(config->with_nitrogen) */

    if(config->with_nitrogen && !config->npp_controlled_bnf)
    {
      bnf=biologicalnfixation(stand, npft, ncft, config);
      stand->soil.NH4[0]+=bnf;
      getoutput(&cell->output,BNF,config)+=bnf*stand->frac;
      if(stand->type->landusetype!=NATURAL && stand->type->landusetype!=WOODPLANTATION)
        getoutput(&cell->output,BNF_MG,config)+=bnf*stand->frac;
      cell->balance.influx.nitrogen+=bnf*stand->frac;
    }

    runoff=daily_stand(stand,co2,&climate,day,month,daylength,
                       gtemp_air,gtemp_soil[0],eeq,par,
                       melt,npft,ncft,year,intercrop,agrfrac,config);
    if(config->with_nitrogen)
    {
      denitrification(stand,npft,ncft,config);

      nh3=volatilization(stand->soil.NH4[0],climate.windspeed,climate.temp,
                         length,cell->soilph);
      if(nh3>stand->soil.NH4[0])
        nh3=stand->soil.NH4[0];
      stand->soil.NH4[0]-=nh3;
      getoutput(&cell->output,N_VOLATILIZATION,config)+=nh3*stand->frac;
      if (isagriculture(stand->type->landusetype))
        getoutput(&cell->output,NH3_AGR,config)+=nh3*stand->frac;
      if(stand->type->landusetype==GRASSLAND)
        getoutput(&cell->output,NH3_MGRASS,config)+=nh3*stand->frac;

      cell->balance.n_outflux+=nh3*stand->frac;
    }

    cell->discharge.drunoff+=runoff*stand->frac;
    climate.prec=prec_save;
    foreachpft(pft, p, &stand->pftlist)
      getoutput(&cell->output,VEGC_AVG,config)+=vegc_sum(pft)*stand->frac*(1.0/(1-cell->lakefrac-cell->ml.reservoirfrac));
    getoutput(&cell->output,SWC1,config)+=(stand->soil.w[0]*stand->soil.whcs[0]+stand->soil.w_fw[0]+stand->soil.wpwps[0]+
              stand->soil.ice_depth[0]+stand->soil.ice_fw[0])/stand->soil.wsats[0]*stand->frac*(1.0/(1-cell->lakefrac-cell->ml.reservoirfrac));
    getoutput(&cell->output,SWC2,config)+=(stand->soil.w[1]*stand->soil.whcs[1]+stand->soil.w_fw[1]+stand->soil.wpwps[1]+
              stand->soil.ice_depth[1]+stand->soil.ice_fw[1])/stand->soil.wsats[1]*stand->frac*(1.0/(1-cell->lakefrac-cell->ml.reservoirfrac));
    getoutput(&cell->output,SWC3,config)+=(stand->soil.w[2]*stand->soil.whcs[2]+stand->soil.w_fw[2]+stand->soil.wpwps[2]+
              stand->soil.ice_depth[2]+stand->soil.ice_fw[2])/stand->soil.wsats[2]*stand->frac*(1.0/(1-cell->lakefrac-cell->ml.reservoirfrac));
    getoutput(&cell->output,SWC4,config)+=(stand->soil.w[3]*stand->soil.whcs[3]+stand->soil.w_fw[3]+stand->soil.wpwps[3]+
              stand->soil.ice_depth[3]+stand->soil.ice_fw[3])/stand->soil.wsats[3]*stand->frac*(1.0/(1-cell->lakefrac-cell->ml.reservoirfrac));
    getoutput(&cell->output,SWC5,config)+=(stand->soil.w[4]*stand->soil.whcs[4]+stand->soil.w_fw[4]+stand->soil.wpwps[4]+
              stand->soil.ice_depth[4]+stand->soil.ice_fw[4])/stand->soil.wsats[4]*stand->frac*(1.0/(1-cell->lakefrac-cell->ml.reservoirfrac));
    foreachsoillayer(l)
    {
      getoutputindex(&cell->output,SWC,l,config)+=(stand->soil.w[l]*stand->soil.whcs[l]+stand->soil.w_fw[l]+stand->soil.wpwps[l]+
                     stand->soil.ice_depth[l]+stand->soil.ice_fw[l])/stand->soil.wsats[l]*stand->frac*(1.0/(1-cell->lakefrac-cell->ml.reservoirfrac));
      getoutput(&cell->output,TWS,config)+=(stand->soil.w[l]*stand->soil.whcs[l]+stand->soil.w_fw[l]+stand->soil.wpwps[l]+
                     stand->soil.ice_depth[l]+stand->soil.ice_fw[l])*stand->frac;
    }
    forrootmoist(l)
    {
      getoutput(&cell->output,ROOTMOIST,config)+=stand->soil.w[l]*stand->soil.whcs[l]*stand->frac; /* absolute soil water content between wilting point and field capacity (mm) */
      if isagriculture(stand->type->landusetype)
        getoutput(&cell->output,ROOTMOIST_AGR,config)+=stand->soil.w[l]*stand->soil.whcs[l]*stand->frac*(1.0/(1-stand->cell->lakefrac-stand->cell->ml.reservoirfrac)); /* absolute soil water content between wilting point and field capacity (mm) */
    }

    if(stand->type->landusetype==GRASSLAND || stand->type->landusetype==OTHERS ||
       stand->type->landusetype==AGRICULTURE || stand->type->landusetype==AGRICULTURE_GRASS || stand->type->landusetype==AGRICULTURE_TREE ||
       stand->type->landusetype==BIOMASS_TREE || stand->type->landusetype==BIOMASS_GRASS || stand->type->landusetype==WOODPLANTATION)
    {
      data = stand->data;
      if(data->irrigation)
      {
        getoutput(&cell->output,IRRIG_STOR,config)+=data->irrig_stor*stand->frac*cell->coord.area;
        getoutput(&cell->output,TWS,config)+=data->irrig_stor*stand->frac;
      }
    }
    /* only first 5 layers for SWC_VOL output */
    forrootsoillayer(l)
    {
      getoutputindex(&cell->output,SWC_VOL,l,config)+=(stand->soil.w[l]*stand->soil.whcs[l]+stand->soil.w_fw[l]+stand->soil.wpwps[l]+
                     stand->soil.ice_depth[l]+stand->soil.ice_fw[l])*stand->frac*cell->coord.area;
    }
  } /* of foreachstand */

  getoutput(&cell->output,CELLFRAC_AGR,config)+=agrfrac;
  getoutput(&cell->output,DECAY_LEAF_NV,config)*=litsum_old_nv[LEAF]>0 ? litsum_new_nv[LEAF]/litsum_old_nv[LEAF] : 1;
  getoutput(&cell->output,DECAY_WOOD_NV,config)*=litsum_old_nv[WOOD]>0 ? litsum_new_nv[WOOD]/litsum_old_nv[WOOD] : 1;
  getoutput(&cell->output,DECAY_LEAF_AGR,config)*=litsum_old_agr[LEAF]>0 ? litsum_new_agr[LEAF]/litsum_old_agr[LEAF] : 1;
  getoutput(&cell->output,DECAY_WOOD_AGR,config)*=litsum_old_agr[WOOD]>0 ? litsum_new_agr[WOOD]/litsum_old_agr[WOOD] : 1;


#ifdef IMAGE
  // outflow from groundwater reservoir to river
  if (cell->discharge.dmass_gw > 0)
  {
    fout_gw=cell->discharge.dmass_gw/GWCOEFF;
    cell->discharge.drunoff+=fout_gw/cell->coord.area;
    cell->discharge.dmass_gw-=fout_gw;
    getoutput(&cell->output,SEEPAGE,config)+=fout_gw/cell->coord.area;
  }
#endif

  getoutput(&cell->output,RUNOFF,config)+=cell->discharge.drunoff;
  cell->balance.awater_flux+=cell->discharge.drunoff;
  if(config->with_lakes)
  {
    radiation(&daylength,&par,&eeq,cell->coord.lat,day,&climate,c_albwater,config->with_radiation);
    getoutput(&cell->output,PET,config)+=eeq*PRIESTLEY_TAYLOR*(cell->lakefrac+cell->ml.reservoirfrac);
    cell->output.mpet+=eeq*PRIESTLEY_TAYLOR*(cell->lakefrac+cell->ml.reservoirfrac);
    getoutput(&cell->output,ALBEDO,config)+=c_albwater*(cell->lakefrac+cell->ml.reservoirfrac);

    /* reservoir waterbalance */
    if(cell->ml.dam)
      update_reservoir_daily(cell,climate.prec,eeq,month,config);

    /* lake waterbalance */
    cell->discharge.dmass_lake+=climate.prec*cell->coord.area*cell->lakefrac;
    getoutput(&cell->output,INPUT_LAKE,config)+=climate.prec*cell->coord.area*cell->lakefrac;
#ifdef COUPLING_WITH_FMS
    if(cell->discharge.next==-1 && cell->lakefrac>=0.999)
      /*this if statement allows to identify the caspian sea to be an evaporating surface by lakefrac map of lpj and river rooting DDM30 map*/
      /*this does nolonger make sense if discharge next is nolonger -1 (a parameterization of a river rooting map for the casp sea is possebly used
        which is DDM30-coarsemask-zerofill.asc in /p/projects/climber3/gengel/POEM/mom5.0.2/exp/.../Data_for_LPJ), hence discharge next is not -1*/
      {
        /*here evaporation for casp sea is computed*/
        getoutput(&cell->output,EVAP_LAKE,config)+=eeq*PRIESTLEY_TAYLOR*cell->lakefrac;
        cell->balance.aevap_lake+=eeq*PRIESTLEY_TAYLOR*cell->lakefrac;
#if defined IMAGE && defined COUPLED
        if(cell->ml.image_data!=NULL)
          cell->ml.image_data->mevapotr[month] += eeq*PRIESTLEY_TAYLOR*stand->frac;
#endif
        cell->output.dwflux+=eeq*PRIESTLEY_TAYLOR*cell->lakefrac;
        cell->discharge.dmass_lake=cell->discharge.dmass_lake-eeq*PRIESTLEY_TAYLOR*cell->coord.area*cell->lakefrac;
      }
    else if(cell->discharge.next==-9)/*discharge for ocean cells, that are threated as land by lpj on land lad resolution is computed here*/
      {
        /*
        if (cell->coord.lat<-60) //we have to exclude antarctica here since cells there have cell->discharge.next==-9 and lakefrac1 following initialization.  They should not contribute to evap of lakes here
          {
            cell->output.mevap_lake+=0;
            cell->discharge.dmass_lake=0.0;
          }
            
          else1.4.2016  changed the grid initialization in newgrid.c such that we have here no problem anymore, since the lakefraction now is nearly zero everywhere. */
          {
            getoutput(&cell->output,EVAP_LAKE,config)+=eeq*PRIESTLEY_TAYLOR*cell->lakefrac;
            cell->balance.aevap_lake+=eeq*PRIESTLEY_TAYLOR*cell->lakefrac;
#if defined IMAGE && defined COUPLED
            if(cell->ml.image_data!=NULL)
              cell->ml.image_data->mevapotr[month] += =eeq*PRIESTLEY_TAYLOR*stand->frac;
#endif
            cell->discharge.dmass_lake=max(cell->discharge.dmass_lake-eeq*PRIESTLEY_TAYLOR*cell->coord.area*cell->lakefrac,0.0);
          }
      }
    else
#endif
    {
    getoutput(&cell->output,EVAP_LAKE,config)+=min(cell->discharge.dmass_lake/cell->coord.area,eeq*PRIESTLEY_TAYLOR*cell->lakefrac);
    cell->balance.aevap_lake+=min(cell->discharge.dmass_lake/cell->coord.area,eeq*PRIESTLEY_TAYLOR*cell->lakefrac);
#if defined IMAGE && defined COUPLED
     if(cell->ml.image_data!=NULL)
       cell->ml.image_data->mevapotr[month] += min(cell->discharge.dmass_lake/cell->coord.area,eeq*PRIESTLEY_TAYLOR*cell->lakefrac);
#endif
#ifdef COUPLING_WITH_FMS
    cell->output.dwflux+=min(cell->discharge.dmass_lake/cell->coord.area,eeq*PRIESTLEY_TAYLOR*cell->lakefrac);
#endif
    cell->discharge.dmass_lake=max(cell->discharge.dmass_lake-eeq*PRIESTLEY_TAYLOR*cell->coord.area*cell->lakefrac,0.0);
    }

    getoutput(&cell->output,LAKEVOL,config)+=cell->discharge.dmass_lake;
    getoutput(&cell->output,RIVERVOL,config)+=cell->discharge.dmass_river;
    getoutput(&cell->output,TWS,config)+=(cell->discharge.dmass_lake+cell->discharge.dmass_river)/cell->coord.area;
  } /* of 'if(river_routing)' */
  getoutput(&cell->output,DAYLENGTH,config)+=daylength;
  soilpar_output(cell,agrfrac,config);
  killstand(cell,npft, ncft,cell->ml.with_tillage,intercrop,year,config);
#ifdef SAFE
  check_stand_fracs(cell,cell->lakefrac+cell->ml.reservoirfrac);
#endif
  /* Establishment fluxes are area weighted in subroutines */
  getoutput(&cell->output,FLUX_ESTABC,config)+=flux_estab.carbon;
  getoutput(&cell->output,FLUX_ESTABN,config)+=flux_estab.nitrogen;
  cell->balance.flux_estab.nitrogen+=flux_estab.nitrogen;
  cell->balance.flux_estab.carbon+=flux_estab.carbon;
  cell->output.dcflux-=flux_estab.carbon;
} /* of 'update_daily' */
