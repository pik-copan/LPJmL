/*
The function calculates thermal properties of the soil for the different vertical layers.
It returs volumetric heat capacity, and thermal conductivity for the soil in frozen and 
unfrozen state based on soil texture and water content.
For the conductivity it uses the approach described by Johansen (1977)
*/

#define K_SOLID 8       /* Thermal conductivity of solid components in saturated state */
#define K_ICE   2.2     /* Thermal conductivity of ice */
#define K_WATER 0.57    /* Thermal conductivity of liquid water*/

#include "lpj.h"

void calc_soil_thermal_properties(Soil_thermal_prop *th,    /*< Soil thermal property structure that is set or modified */
                     const Soil *soil,         /*< Soil structure from which water content etc is obtained  */
                     const Real *waterc_abs,
                     const Real *porosity_rel,
                     Bool johansen,            /*< Flag to activate johansen method */
                     Bool with_conductivity    /*< Flag to activate conductivity update  */
                     ) 
{
  int    layer, j;
  Real c_froz, c_unfroz;             /* frozen and unfrozen heat capacities */
  Real lam_froz, lam_unfroz;         /* frozen and unfrozen conductivities */
  Real latent_heat;                  /* latent heat of fusion depending on water content */
  Real lam_sat_froz, lam_sat_unfroz; /* frozen and unfrozen conductivities for saturated soil */
  Real waterc_abs_layer;             /* total absolute water content of soil */
  Real solidc_abs_layer;             /* total absolute solid content of soil */
  Real sat;                          /* saturation of soil */
  Real ke_unfroz, ke_froz;           /* kersten number for unfrozen and frozen soil */
  Real por;                          /* porosity of the soil */

  Real tmp;                          /* temporary variable */

  for (layer = 0; layer < NSOILLAYER; ++layer) {

    if (!johansen)
    {
     // fail(-1, TRUE, "Currently only the Johansen method to\ 
     //                 calculate soil thermal conductivities is implemented.");
     printf("Only Johansen implemented");

    }

    /* get absolute water and solid content of soil */
    if(waterc_abs == NULL){
      waterc_abs_layer = allwater(soil,layer)+allice(soil,layer);
    }
    else 
      waterc_abs_layer = waterc_abs[layer];

    if(porosity_rel == NULL)
      solidc_abs_layer = soildepth[layer] - soil->wsats[layer];
    else 
      solidc_abs_layer = soildepth[layer] - soildepth[layer]*porosity_rel[layer];    
     
    if(with_conductivity)
    {
      /* get frozen and unfrozen conductivity with johansens approach */
      por            = soil -> wsat[layer];
      tmp = pow( K_SOLID, (1 - por));
      lam_sat_froz   =  tmp * pow(K_ICE, por); /* geometric mean  */
      lam_sat_unfroz =  tmp * pow(K_WATER, por);
      if(soil->wsats[layer]<epsilon)
        sat=0;
      else
        sat        =  waterc_abs_layer / soil->wsats[layer];
      ke_unfroz  = (sat < 0.1 ? 0 : log10(sat) + 1); /* fine soil parametrisation of Johansen */
      ke_froz    =  sat;
      lam_froz   = (lam_sat_froz   - soil->k_dry[layer]) * ke_froz   + soil->k_dry[layer]; 
      lam_unfroz = (lam_sat_unfroz - soil->k_dry[layer]) * ke_unfroz + soil->k_dry[layer];
    }
    /* get frozen and unfrozen volumetric heat capacity */
    c_froz   = (c_mineral * solidc_abs_layer + c_ice   * waterc_abs_layer) / soildepth[layer];
    c_unfroz = (c_mineral * solidc_abs_layer + c_water * waterc_abs_layer) / soildepth[layer];

    /* get volumetric latent heat   */
    latent_heat = waterc_abs_layer / soildepth[layer] * c_water2ice;

    for (j = 0; j < GPLHEAT; ++j) { /* iterate through gridpoints of the layer */

      if(with_conductivity)
      {
        /* set properties of j-th layer element */
        /* maximum element refernced = GPLHEAT*(NSOILLAYER-1)+GPLHEAT-1 = NHEATGRIDP-1 */
        th->lam_frozen[GPLHEAT * layer + j]   = lam_froz;
        th->lam_unfrozen[GPLHEAT * layer + j] = lam_unfroz;
      }
      /* set properties of j-th layer gridpoint */
      th->c_frozen [GPLHEAT * layer + j]    = c_froz;
      th->c_unfrozen[GPLHEAT * layer + j]   = c_unfroz;
      th->latent_heat[GPLHEAT * layer + j]  = latent_heat;
    }
  }
}
