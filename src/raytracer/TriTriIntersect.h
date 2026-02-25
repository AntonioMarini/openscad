/* Triangle/triangle intersection test routine,
* by Tomas Moller, 1997.
 * See article "A Fast Triangle-Triangle Intersection Test",
 * Journal of Graphics Tools, 2(2), 1997
 * updated: 2001-06-20 (added line of intersection)
 *
 * int tri_tri_intersect(float V0[3],float V1[3],float V2[3],
 *                       float U0[3],float U1[3],float U2[3])
 *
 * parameters: vertices of triangle 1: V0,V1,V2
 *             vertices of triangle 2: U0,U1,U2
 * result    : returns 1 if the triangles intersect, otherwise 0
 *
 * Here is a version withouts divisions (a little faster)
 * int NoDivTriTriIsect(float V0[3],float V1[3],float V2[3],
 *                      float U0[3],float U1[3],float U2[3]);
 *
 * This version computes the line of intersection as well (if they are not coplanar):
 * int tri_tri_intersect_with_isectline(float V0[3],float V1[3],float V2[3],
 *				        float U0[3],float U1[3],float U2[3],int *coplanar,
 *				        float isectpt1[3],float isectpt2[3]);
 * coplanar returns whether the tris are coplanar
 * isectpt1, isectpt2 are the endpoints of the line of intersection
 */

#ifndef TRI_TRI_INTERSECT_H
#define TRI_TRI_INTERSECT_H

#include <cmath>

#define TRI_FABS(x) ((x)>=0?(x):-(x))

#define TRI_USE_EPSILON_TEST 1
#define TRI_EPSILON 0.000001f

#define TRI_CROSS(dest,v1,v2)                      \
              dest[0]=v1[1]*v2[2]-v1[2]*v2[1]; \
              dest[1]=v1[2]*v2[0]-v1[0]*v2[2]; \
              dest[2]=v1[0]*v2[1]-v1[1]*v2[0];

#define TRI_DOT(v1,v2) (v1[0]*v2[0]+v1[1]*v2[1]+v1[2]*v2[2])

#define TRI_SUB(dest,v1,v2) dest[0]=v1[0]-v2[0]; dest[1]=v1[1]-v2[1]; dest[2]=v1[2]-v2[2];

#define TRI_ADD(dest,v1,v2) dest[0]=v1[0]+v2[0]; dest[1]=v1[1]+v2[1]; dest[2]=v1[2]+v2[2];

#define TRI_MULT(dest,v,factor) dest[0]=factor*v[0]; dest[1]=factor*v[1]; dest[2]=factor*v[2];

#define TRI_SET(dest,src) dest[0]=src[0]; dest[1]=src[1]; dest[2]=src[2];

#define TRI_SORT(a,b)       \
             if(a>b)    \
             {          \
               float c; \
               c=a;     \
               a=b;     \
               b=c;     \
             }

#define TRI_SORT2(a,b,smallest)       \
             if(a>b)       \
             {             \
               float c;    \
               c=a;        \
               a=b;        \
               b=c;        \
               smallest=1; \
             }             \
             else smallest=0;

#define TRI_ISECT(VV0,VV1,VV2,D0,D1,D2,isect0,isect1) \
              isect0=VV0+(VV1-VV0)*D0/(D0-D1);    \
              isect1=VV0+(VV2-VV0)*D0/(D0-D2);

#define TRI_EDGE_EDGE_TEST(V0,U0,U1)                      \
  Bx=U0[i0]-U1[i0];                                   \
  By=U0[i1]-U1[i1];                                   \
  Cx=V0[i0]-U0[i0];                                   \
  Cy=V0[i1]-U0[i1];                                   \
  f=Ay*Bx-Ax*By;                                      \
  d=By*Cx-Bx*Cy;                                      \
  if((f>0 && d>=0 && d<=f) || (f<0 && d<=0 && d>=f))  \
  {                                                   \
    e=Ax*Cy-Ay*Cx;                                    \
    if(f>0)                                           \
    {                                                 \
      if(e>=0 && e<=f) return 1;                      \
    }                                                 \
    else                                              \
    {                                                 \
      if(e<=0 && e>=f) return 1;                      \
    }                                                 \
  }

#define TRI_EDGE_AGAINST_TRI_EDGES(V0,V1,U0,U1,U2) \
{                                              \
  float Ax,Ay,Bx,By,Cx,Cy,e,d,f;               \
  Ax=V1[i0]-V0[i0];                            \
  Ay=V1[i1]-V0[i1];                            \
  TRI_EDGE_EDGE_TEST(V0,U0,U1);                    \
  TRI_EDGE_EDGE_TEST(V0,U1,U2);                    \
  TRI_EDGE_EDGE_TEST(V0,U2,U0);                    \
}

#define TRI_POINT_IN_TRI(V0,U0,U1,U2)           \
{                                           \
  float a,b,c,d0,d1,d2;                     \
  a=U1[i1]-U0[i1];                          \
  b=-(U1[i0]-U0[i0]);                       \
  c=-a*U0[i0]-b*U0[i1];                     \
  d0=a*V0[i0]+b*V0[i1]+c;                   \
                                            \
  a=U2[i1]-U1[i1];                          \
  b=-(U2[i0]-U1[i0]);                       \
  c=-a*U1[i0]-b*U1[i1];                     \
  d1=a*V0[i0]+b*V0[i1]+c;                   \
                                            \
  a=U0[i1]-U2[i1];                          \
  b=-(U0[i0]-U2[i0]);                       \
  c=-a*U2[i0]-b*U2[i1];                     \
  d2=a*V0[i0]+b*V0[i1]+c;                   \
  if(d0*d1>0.0)                             \
  {                                         \
    if(d0*d2>0.0) return 1;                 \
  }                                         \
}

inline int tri_coplanar_tri_tri(float N[3],float V0[3],float V1[3],float V2[3],
                                float U0[3],float U1[3],float U2[3])
{
   float A[3];
   short i0,i1;
   A[0]=TRI_FABS(N[0]);
   A[1]=TRI_FABS(N[1]);
   A[2]=TRI_FABS(N[2]);
   if(A[0]>A[1])
   {
      if(A[0]>A[2]) { i0=1; i1=2; }
      else { i0=0; i1=1; }
   }
   else
   {
      if(A[2]>A[1]) { i0=0; i1=1; }
      else { i0=0; i1=2; }
   }

    TRI_EDGE_AGAINST_TRI_EDGES(V0,V1,U0,U1,U2);
    TRI_EDGE_AGAINST_TRI_EDGES(V1,V2,U0,U1,U2);
    TRI_EDGE_AGAINST_TRI_EDGES(V2,V0,U0,U1,U2);

    TRI_POINT_IN_TRI(V0,U0,U1,U2);
    TRI_POINT_IN_TRI(U0,V0,V1,V2);

    return 0;
}

#define TRI_COMPUTE_INTERVALS(VV0,VV1,VV2,D0,D1,D2,D0D1,D0D2,isect0,isect1) \
  if(D0D1>0.0f)                                         \
  {                                                     \
    TRI_ISECT(VV2,VV0,VV1,D2,D0,D1,isect0,isect1);     \
  }                                                     \
  else if(D0D2>0.0f)                                    \
  {                                                     \
    TRI_ISECT(VV1,VV0,VV2,D1,D0,D2,isect0,isect1);     \
  }                                                     \
  else if(D1*D2>0.0f || D0!=0.0f)                       \
  {                                                     \
    TRI_ISECT(VV0,VV1,VV2,D0,D1,D2,isect0,isect1);     \
  }                                                     \
  else if(D1!=0.0f)                                     \
  {                                                     \
    TRI_ISECT(VV1,VV0,VV2,D1,D0,D2,isect0,isect1);     \
  }                                                     \
  else if(D2!=0.0f)                                     \
  {                                                     \
    TRI_ISECT(VV2,VV0,VV1,D2,D0,D1,isect0,isect1);     \
  }                                                     \
  else                                                  \
  {                                                     \
    return tri_coplanar_tri_tri(N1,V0,V1,V2,U0,U1,U2);  \
  }

inline int tri_tri_intersect(float V0[3],float V1[3],float V2[3],
                             float U0[3],float U1[3],float U2[3])
{
  float E1[3],E2[3];
  float N1[3],N2[3],d1,d2;
  float du0,du1,du2,dv0,dv1,dv2;
  float D[3];
  float isect1[2], isect2[2];
  float du0du1,du0du2,dv0dv1,dv0dv2;
  short index;
  float vp0,vp1,vp2;
  float up0,up1,up2;
  float b,c,max;

  TRI_SUB(E1,V1,V0);
  TRI_SUB(E2,V2,V0);
  TRI_CROSS(N1,E1,E2);
  d1=-TRI_DOT(N1,V0);

  du0=TRI_DOT(N1,U0)+d1;
  du1=TRI_DOT(N1,U1)+d1;
  du2=TRI_DOT(N1,U2)+d1;

#if TRI_USE_EPSILON_TEST
  if(TRI_FABS(du0)<TRI_EPSILON) du0=0.0;
  if(TRI_FABS(du1)<TRI_EPSILON) du1=0.0;
  if(TRI_FABS(du2)<TRI_EPSILON) du2=0.0;
#endif
  du0du1=du0*du1;
  du0du2=du0*du2;

  if(du0du1>0.0f && du0du2>0.0f) return 0;

  TRI_SUB(E1,U1,U0);
  TRI_SUB(E2,U2,U0);
  TRI_CROSS(N2,E1,E2);
  d2=-TRI_DOT(N2,U0);

  dv0=TRI_DOT(N2,V0)+d2;
  dv1=TRI_DOT(N2,V1)+d2;
  dv2=TRI_DOT(N2,V2)+d2;

#if TRI_USE_EPSILON_TEST
  if(TRI_FABS(dv0)<TRI_EPSILON) dv0=0.0;
  if(TRI_FABS(dv1)<TRI_EPSILON) dv1=0.0;
  if(TRI_FABS(dv2)<TRI_EPSILON) dv2=0.0;
#endif

  dv0dv1=dv0*dv1;
  dv0dv2=dv0*dv2;

  if(dv0dv1>0.0f && dv0dv2>0.0f) return 0;

  TRI_CROSS(D,N1,N2);

  max=TRI_FABS(D[0]);
  index=0;
  b=TRI_FABS(D[1]);
  c=TRI_FABS(D[2]);
  if(b>max) max=b,index=1;
  if(c>max) max=c,index=2;

  vp0=V0[index];
  vp1=V1[index];
  vp2=V2[index];

  up0=U0[index];
  up1=U1[index];
  up2=U2[index];

  TRI_COMPUTE_INTERVALS(vp0,vp1,vp2,dv0,dv1,dv2,dv0dv1,dv0dv2,isect1[0],isect1[1]);
  TRI_COMPUTE_INTERVALS(up0,up1,up2,du0,du1,du2,du0du1,du0du2,isect2[0],isect2[1]);

  TRI_SORT(isect1[0],isect1[1]);
  TRI_SORT(isect2[0],isect2[1]);

  if(isect1[1]<isect2[0] || isect2[1]<isect1[0]) return 0;
  return 1;
}

inline void tri_isect2(float VTX0[3],float VTX1[3],float VTX2[3],
                       float VV0,float VV1,float VV2,
                       float D0,float D1,float D2,
                       float *isect0,float *isect1,
                       float isectpoint0[3],float isectpoint1[3])
{
  float tmp=D0/(D0-D1);
  float diff[3];
  *isect0=VV0+(VV1-VV0)*tmp;
  TRI_SUB(diff,VTX1,VTX0);
  TRI_MULT(diff,diff,tmp);
  TRI_ADD(isectpoint0,diff,VTX0);
  tmp=D0/(D0-D2);
  *isect1=VV0+(VV2-VV0)*tmp;
  TRI_SUB(diff,VTX2,VTX0);
  TRI_MULT(diff,diff,tmp);
  TRI_ADD(isectpoint1,VTX0,diff);
}

inline int tri_compute_intervals_isectline(float VERT0[3],float VERT1[3],float VERT2[3],
                                           float VV0,float VV1,float VV2,
                                           float D0,float D1,float D2,
                                           float D0D1,float D0D2,
                                           float *isect0,float *isect1,
                                           float isectpoint0[3],float isectpoint1[3])
{
  if(D0D1>0.0f)
  {
    tri_isect2(VERT2,VERT0,VERT1,VV2,VV0,VV1,D2,D0,D1,isect0,isect1,isectpoint0,isectpoint1);
  }
  else if(D0D2>0.0f)
  {
    tri_isect2(VERT1,VERT0,VERT2,VV1,VV0,VV2,D1,D0,D2,isect0,isect1,isectpoint0,isectpoint1);
  }
  else if(D1*D2>0.0f || D0!=0.0f)
  {
    tri_isect2(VERT0,VERT1,VERT2,VV0,VV1,VV2,D0,D1,D2,isect0,isect1,isectpoint0,isectpoint1);
  }
  else if(D1!=0.0f)
  {
    tri_isect2(VERT1,VERT0,VERT2,VV1,VV0,VV2,D1,D0,D2,isect0,isect1,isectpoint0,isectpoint1);
  }
  else if(D2!=0.0f)
  {
    tri_isect2(VERT2,VERT0,VERT1,VV2,VV0,VV1,D2,D0,D1,isect0,isect1,isectpoint0,isectpoint1);
  }
  else
  {
    return 1;
  }
  return 0;
}

inline int tri_tri_intersect_with_isectline(float V0[3],float V1[3],float V2[3],
                                            float U0[3],float U1[3],float U2[3],
                                            int *coplanar,
                                            float isectpt1[3],float isectpt2[3])
{
  float E1[3],E2[3];
  float N1[3],N2[3],d1,d2;
  float du0,du1,du2,dv0,dv1,dv2;
  float D[3];
  float isect1[2], isect2[2];
  float isectpointA1[3],isectpointA2[3];
  float isectpointB1[3],isectpointB2[3];
  float du0du1,du0du2,dv0dv1,dv0dv2;
  short index;
  float vp0,vp1,vp2;
  float up0,up1,up2;
  float b,c,max;
  int smallest1,smallest2;

  TRI_SUB(E1,V1,V0);
  TRI_SUB(E2,V2,V0);
  TRI_CROSS(N1,E1,E2);
  d1=-TRI_DOT(N1,V0);

  du0=TRI_DOT(N1,U0)+d1;
  du1=TRI_DOT(N1,U1)+d1;
  du2=TRI_DOT(N1,U2)+d1;

#if TRI_USE_EPSILON_TEST
  if(TRI_FABS(du0)<TRI_EPSILON) du0=0.0;
  if(TRI_FABS(du1)<TRI_EPSILON) du1=0.0;
  if(TRI_FABS(du2)<TRI_EPSILON) du2=0.0;
#endif
  du0du1=du0*du1;
  du0du2=du0*du2;

  if(du0du1>0.0f && du0du2>0.0f) return 0;

  TRI_SUB(E1,U1,U0);
  TRI_SUB(E2,U2,U0);
  TRI_CROSS(N2,E1,E2);
  d2=-TRI_DOT(N2,U0);

  dv0=TRI_DOT(N2,V0)+d2;
  dv1=TRI_DOT(N2,V1)+d2;
  dv2=TRI_DOT(N2,V2)+d2;

#if TRI_USE_EPSILON_TEST
  if(TRI_FABS(dv0)<TRI_EPSILON) dv0=0.0;
  if(TRI_FABS(dv1)<TRI_EPSILON) dv1=0.0;
  if(TRI_FABS(dv2)<TRI_EPSILON) dv2=0.0;
#endif

  dv0dv1=dv0*dv1;
  dv0dv2=dv0*dv2;

  if(dv0dv1>0.0f && dv0dv2>0.0f) return 0;

  TRI_CROSS(D,N1,N2);

  max=TRI_FABS(D[0]);
  index=0;
  b=TRI_FABS(D[1]);
  c=TRI_FABS(D[2]);
  if(b>max) max=b,index=1;
  if(c>max) max=c,index=2;

  vp0=V0[index];
  vp1=V1[index];
  vp2=V2[index];

  up0=U0[index];
  up1=U1[index];
  up2=U2[index];

  *coplanar=tri_compute_intervals_isectline(V0,V1,V2,vp0,vp1,vp2,dv0,dv1,dv2,
                                            dv0dv1,dv0dv2,&isect1[0],&isect1[1],
                                            isectpointA1,isectpointA2);
  if(*coplanar) return tri_coplanar_tri_tri(N1,V0,V1,V2,U0,U1,U2);

  tri_compute_intervals_isectline(U0,U1,U2,up0,up1,up2,du0,du1,du2,
                                  du0du1,du0du2,&isect2[0],&isect2[1],
                                  isectpointB1,isectpointB2);

  TRI_SORT2(isect1[0],isect1[1],smallest1);
  TRI_SORT2(isect2[0],isect2[1],smallest2);

  if(isect1[1]<isect2[0] || isect2[1]<isect1[0]) return 0;

  if(isect2[0]<isect1[0])
  {
    if(smallest1==0) { TRI_SET(isectpt1,isectpointA1); }
    else { TRI_SET(isectpt1,isectpointA2); }

    if(isect2[1]<isect1[1])
    {
      if(smallest2==0) { TRI_SET(isectpt2,isectpointB2); }
      else { TRI_SET(isectpt2,isectpointB1); }
    }
    else
    {
      if(smallest1==0) { TRI_SET(isectpt2,isectpointA2); }
      else { TRI_SET(isectpt2,isectpointA1); }
    }
  }
  else
  {
    if(smallest2==0) { TRI_SET(isectpt1,isectpointB1); }
    else { TRI_SET(isectpt1,isectpointB2); }

    if(isect2[1]>isect1[1])
    {
      if(smallest1==0) { TRI_SET(isectpt2,isectpointA2); }
      else { TRI_SET(isectpt2,isectpointA1); }
    }
    else
    {
      if(smallest2==0) { TRI_SET(isectpt2,isectpointB2); }
      else { TRI_SET(isectpt2,isectpointB1); }
    }
  }
  return 1;
}

struct Triangle {
  Eigen::Vector3f v0, v1, v2;
};

#endif