/*----------------------------------------------------------------------------*/
/* Reversible random number generator                                         */
/*----------------------------------------------------------------------------*/
#define H 32768               /* = 2^15 : use in MultModM */
#define RandUnif(G) RNGGenVal(G)
#define RandReverseUnif(G) RNGGenReverseVal(G)
#define RandExponential(G,lambda) (-(lambda)*log(RandUnif(G)))
#define RandReverseExponential(G,lambda) RandReverseUnif(G)

/*----------------------------------------------------------------------------*/
typedef  unsigned long Generator;
typedef  enum {InitialSeed, LastSeed, NewSeed}  SeedType;

/*----------------------------------------------------------------------------*/
static long aw[4], avw[4],    
    a[4] = { 45991, 207707, 138556, 49689 },
    m[4] = { 2147483647, 2147483543, 2147483423, 2147483323 };
static long long b[4] = {0, 0, 0, 0}; /* equals a[i]^{m[i]-2} mod m[i] */
static long *Ig[4], *Lg[4],  *Cg[4]; 
static short i, j;
unsigned int Maxgen = 0;

/*----------------------------------------------------------------------------*/
long long FindB( long long a, long long k, long long m )
{ 
   int i;
   long long sqrs[32];
   long long power_of_2;
   long long b;

   sqrs[0] = a;
   for( i = 1; i < 32; i++ )
   {
      sqrs[i] = (sqrs[i-1] * sqrs[i-1]) % m;
   }

   power_of_2 = 1;   
   b = 1;
   for( i = 0; i < 32; i++ )
   {
      if( !(power_of_2 & k) )
      {
         sqrs[i] = 1;

      }

      b = (b * sqrs[i]) % m;
      power_of_2 = power_of_2 * 2;
   }

   return(b);
}

/*----------------------------------------------------------------------------*/
static long MultModM (long s, long t, long M)
   /* Returns (s*t) MOD M.  Assumes that -M < s < M and -M < t < M.    */
   /* See L'Ecuyer and Cote (1991).                                    */
{
  long R, S0, S1, q, qh, rh, k;

  if (s < 0)  s += M;
  if (t < 0)  t += M;
  if (s < H)  { S0 = s;  R = 0; }
  else
  {
    S1 = s/H;  S0 = s - H*S1;
    qh = M/H;  rh = M - H*qh;
    if (S1 >= H)
    {
      S1 -= H;   k = t/qh;   R = H * (t - k*qh) - k*rh;
      while (R < 0)  R += M;
    }
    else R = 0;
    if (S1 != 0)
    {
      q = M/S1;   k = t/q;   R -= k * (M - S1*q);
      if (R > 0)  R -= M;
      R += S1*(t - k*q);
      while (R < 0)  R += M;
    }
    k = R/qh;   R = H * (R - k*qh) - k*rh;
    while (R < 0) R += M;
  }
  if (S0 != 0)
  {
    q = M/S0;   k = t/q;   R -= k* (M - S0*q);
    if (R > 0)  R -= M;
    R += S0 * (t - k*q);
    while (R < 0)  R += M;
  }
  return R;
}

/*----------------------------------------------------------------------------*/
static double RNGGenVal (Generator g)
{
  long k,s;
  double u;
  u = 0.0;

  if (g > Maxgen || g < 0 ) 
  {
     printf ("clcg4.c: ERROR: GenVal with g > Maxgen or g < 0\n");
     fflush(stdout);
     exit(-1);
  }

  s = Cg [0][g];  k = s / 46693;
  s = 45991 * (s - k * 46693) - k * 25884;
  if (s < 0) s = s + 2147483647;  Cg [0][g] = s;
  u = u + 4.65661287524579692e-10 * s;
 
  s = Cg [1][g];  k = s / 10339;
  s = 207707 * (s - k * 10339) - k * 870;
  if (s < 0) s = s + 2147483543;  Cg [1][g] = s;
  u = u - 4.65661310075985993e-10 * s;
  if (u < 0) u = u + 1.0;

  s = Cg [2][g];  k = s / 15499;
  s = 138556 * (s - k * 15499) - k * 3979;
  if (s < 0.0) s = s + 2147483423;  Cg [2][g] = s;
  u = u + 4.65661336096842131e-10 * s;
  if (u >= 1.0) u = u - 1.0;

  s = Cg [3][g];  k = s / 43218;
  s = 49689 * (s - k * 43218) - k * 24121;
  if (s < 0) s = s + 2147483323;  Cg [3][g] = s;
  u = u - 4.65661357780891134e-10 * s;
  if (u < 0) u = u + 1.0;

  return (u);
}

/*----------------------------------------------------------------------------*/
static void RNGGenReverseVal(Generator g)
{
  long long s;

  if (g > Maxgen || g < 0 ) 
  {
     printf ("clcg4.c: ERROR: GenReverseVal with g > Maxgen or g < 0 \n");
     fflush(stdout);
     exit(-1);
  }

  if( b[0] == 0 )
  {
     printf("clcg4.c: ERROR: b values not calcuated \n"); 
     fflush(stdout);
     exit(-1);
  }

  s = Cg[0][g]; 
  s = (b[0] * s) % m[0];
  Cg[0][g] = s;

  s = Cg[1][g]; 
  s = (b[1] * s) % m[1];
  Cg[1][g] = s;

  s = Cg[2][g]; 
  s = (b[2] * s) % m[2];
  Cg[2][g] = s;

  s = Cg[3][g]; 
  s = (b[3] * s) % m[3];
  Cg[3][g] = s;
}

/*----------------------------------------------------------------------------*/
void RNGInitGenerator (Generator g, SeedType Where)
{
  if (g > Maxgen) 
  {
     printf ("clcg4.c: ERROR: InitGenerator with g > Maxgen \n");
     fflush(stdout);
     exit(-1);
  }

  for (j = 0; j < 4; j++)
  {
    switch (Where)
    {
      case InitialSeed :
        Lg [j][g] = Ig [j][g];   break;
      case NewSeed :
        Lg [j][g] = MultModM (aw [j], Lg [j][g], m [j]);   break;
      case LastSeed :
        break;
    }
    Cg [j][g] = Lg [j][g];
  }
}

/*----------------------------------------------------------------------------*/
void RNGSetInitialSeed (long s[4])
{
  Generator g;
  for (j = 0; j < 4; j++)  Ig [j][0] = s [j];
  RNGInitGenerator (0, InitialSeed);
  for (g = 1; g <= Maxgen; g++)
  {
    if( g % 100000 == 0 )
      printf("clcg4.c: SetInitialSeed complete thru Generator %d\n", (int)g);
    for (j = 0; j < 4; j++)
      Ig [j][g] = MultModM (avw [j], Ig [j][g-1], m [j]);
    RNGInitGenerator (g, InitialSeed);
  }
}

/*----------------------------------------------------------------------------*/
void RNGInit(long v, long w, int maxgen, int pe)
{
  int j;
  long sd[4] = {11111111, 22222222, 33333333, 44444444};
  {sd[0] += 1*pe; sd[1] += 2*pe; sd[2] += 3*pe; sd[3] += 4*pe;}

  SIMDBG(2,pe << ": RNGInit(" << v << ", " << w << ", " << maxgen << ")");

  Maxgen = maxgen;

  for (j = 0; j < 4; j++)
  {
    Ig[j] = (long *)malloc((sizeof( long ) * (Maxgen+1)));
    Lg[j] = (long *)malloc((sizeof( long ) * (Maxgen+1)));
    Cg[j] = (long *)malloc((sizeof( long ) * (Maxgen+1)));

    aw [j] = a [j];
    for (i = 1; i <= w; i++)
      aw [j]  = MultModM (aw [j], aw [j], m[j]);
    avw [j] = aw [j];
    for (i = 1; i <= v; i++)
      avw [j] = MultModM (avw [j], avw [j], m[j]);
  }
  RNGSetInitialSeed (sd);

  for( j = 0; j < 4; j++ )
  {
     b[j] = FindB( a[j], (m[j]-2), m[j] );
  }  
}

/*----------------------------------------------------------------------------*/
void RNGInitDefault (long maxn, int pe)
{
    RNGInit(31, 41, maxn, pe);
}

/*----------------------------------------------------------------------------*/
void RandInit(long maxn, int pe)
{
   RNGInitDefault(maxn, pe);
}

/*----------------------------------------------------------------------------*/
