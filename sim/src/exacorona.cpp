/*------------------------------------------------------------------------*/
/* Author: Kalyan S. Perumalla                                            */
/*------------------------------------------------------------------------*/
#include <mpi.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <iomanip>
#include <musik.h>
#include <fm.h>
#include "json.hpp"
#include "rrng.c"

//-----------------------------------------------------------------------------
using json = nlohmann::json;

//-----------------------------------------------------------------------------
ostream *_exadbgstrm = &cout;
#define EXADBG(level, etc) do{SIMDBGSTRM(_exadbgstrm);SSIMDBG(false,level,etc);}while(0)

//-----------------------------------------------------------------------------
#define DOANIM 0
#if DOANIM
    #define ANIM(_) EXADBG(0,"ANIM "<<_)
    #define ANIMT(_) ANIM(now().ts<<" "<<_)
#else
    #define ANIM(_) (void)0
    #define ANIMT(_) (void)0
#endif

//-----------------------------------------------------------------------------
class Person;
class Location;
class Region;

//-----------------------------------------------------------------------------
static const short UNINFECTED=0, INFECTIOUS=2;

//-----------------------------------------------------------------------------
class GlobalConfig
{
    public: GlobalConfig( void ) {}
    public: virtual ~GlobalConfig() {}
    public: double endtime, lookahead;
    public: long scaledown;
    public: string scenariodir;
    public: struct {string filename;} geo, mob, pttsnorm, pttsvacc;
    public: void load( json &js );
    public: const string &prefdir( const string &fname )
            { static string s; s=scenariodir+"/"+fname; return s; }
} gconfig;

//-----------------------------------------------------------------------------
int string2int( const string &_s )
{
    int retval = 0;
    string s = _s, mil = "million", thou = "thousand";
    size_t i = s.find(mil);
    if( i != std::string::npos )
    {
        s.replace(i, mil.length(), "");
        retval = stoi(s) * 1000000;
    }
    else
    {
        i = s.find(thou);
        if( i != std::string::npos )
        {
            s.replace(i, thou.length(), "");
            retval = stoi(s) * 1000;
        }
        else
        {
            retval = stoi(s);
        }
    }
    return retval;
}

//-----------------------------------------------------------------------------
class InfectionState
{
    public: static const short DEFAULTSTATE = UNINFECTED;
    public: InfectionState( const InfectionState &is ) : bits(is.bits) {}
    public: InfectionState( void ) : bits(0) { set(DEFAULTSTATE); }
    public: virtual ~InfectionState() {}
    public: const InfectionState &operator=( const InfectionState &is )
                { bits = is.bits; return *this; }

    public: typedef unsigned int BitsType;
    public: static unsigned int max( void ) { return sizeof(BitsType)*8; }
    public: bool isset(unsigned int feature)const{return !!(bits & (1<<feature));}
    public: void resetto( unsigned int feature ) { bits=0; set(feature); }
    public: int get( unsigned int i=0 ) const
            {for(int n=max(); i<n; i++){if(isset(i))return i;} return -1;}

    private: void set( unsigned int feature ) { bits |= (1 << feature); }
    private: void unset( unsigned int feature ) { bits &= (~(1 << feature)); }

    private: BitsType bits;
};

//-----------------------------------------------------------------------------
class HealthTransition
{
    public: struct Entry {int j; double prob; struct{double dwelltime;}lo,hi;};
    public: void allocate( void )
            {
                ttablesz = InfectionState::max();
                typedef Entry *EntryStar;
                ttable = new EntryStar[ttablesz];
                for( int i = 0; i < ttablesz; i++ )
                {
                    ttable[i] = new Entry[ttablesz];
                    for( int j = 0; j < ttablesz; j++ )
                    {
                        ttable[i][j].j = j;
                        ttable[i][j].prob = 0.0;
                        ttable[i][j].lo.dwelltime = 0.0;
                        ttable[i][j].hi.dwelltime = 0.0;
                    }
                }
                lastn = 0;
                statenames = new string[ttablesz];
            }
    public: void deallocate( void )
            {
                if( !ttable ) return;
                for( int i = 0; i < ttablesz; i++ )
                {
                    delete [] ttable[i];
                    ttable[i] = 0;
                }
                delete [] ttable; ttable = 0;
                delete [] statenames; statenames = 0;
            }
    public: void copy( const HealthTransition &ht )
            {
                numstates = ht.numstates;
                for( int i = 0; i < ttablesz; i++ )
                {
                    for( int j = 0; j < ttablesz; j++ )
                    {
                        ttable[i][j] = ht.ttable[i][j];
                    }
                    setstatename( i, ht.statenames[i] );
                }
                lastn = ht.lastn;
            }
    public: void loadptts( json &js );
    public: HealthTransition( const HealthTransition &ht )
            { allocate(); copy(ht); }
    public: HealthTransition( const string &jsonfname = "" )
            {
                allocate();

                if( jsonfname != "" )
                {
                    json js; ifstream infs(jsonfname); infs >> js;
                    loadptts( js );
                }
            }
    public: HealthTransition &operator=( const HealthTransition &ht )
            { copy(ht); return *this; }
    public: virtual ~HealthTransition()
            {
                deallocate();
            }
    public: bool verify( void )
            {
                bool valid = true;
                for( int i = 0; i < lastn; i++ )
                {
                    double sum = 0;
                    for( int j = 0; j < lastn; j++ )
                    {
                        double p = ttable[i][j].prob;
                        if( 0 <= p && p <= 1.0 )
                        {
                            sum += ttable[i][j].prob;
                        }
                        else
                        {
                            valid = false;
                            break;
                        }

                        if( ttable[i][j].j != j )
                        {
                            valid = false;
                            break;
                        }
                    }

                    if( !valid || sum != 1.0 )
                    {
                        valid = false;
                        break;
                    }
                }
                return valid;
            }
    public: void setstatename( int i, const string &nm )
            {
                ENSURE( 0, 0 <= i && i < ttablesz, "" );
                statenames[i] = nm;
            }
    public: void addtransition( int i, int j, double p, double low, double high)
            {
                ENSURE( 0, 0 <= i && i < ttablesz, "" );
                ENSURE( 0, 0 <= j && j < ttablesz, "" );
                ENSURE( 0, 0 <= p && p <= 1.0, "" );
                ENSURE( 0, ttable[i][j].j == j, "" );
                ttable[i][j].prob = p;
                ttable[i][j].lo.dwelltime = low;
                ttable[i][j].hi.dwelltime = high;

                lastn = (lastn <= i ? i+1 : lastn);
                lastn = (lastn <= j ? j+1 : lastn);
            }
    public: const Entry &nextstate( int i, double p ) const
            {
                int j = 0;
                double sum = 0;
                ENSURE( 0, 0 <= p && p <= 1.0, "" );
                while( j < lastn )
                {
                    sum += ttable[i][j].prob;
                    if( p <= sum )
                    {
                        break;
                    }
                    else
                    {
                        j++;
                    }
                }
                ENSURE( 0, j < lastn, j << " " << lastn );
                return ttable[i][j];
            }
    private: Entry **ttable;
    private: int ttablesz, lastn;
    private: int numstates;
    private: string *statenames;
};

//-----------------------------------------------------------------------------
class Person
{
    public: typedef unsigned long PersonID;

    public: Person( void ) :
        personid(0), createdat(), age(0),
        vaccinated(false), rng(0), infectts(SimTime::MAX_TIME), istate() {}
    public: Person( const PersonID &_i, const SimPID &_l, const double &_a,
                    const double &_d ) :
        personid(_i), createdat(_l), age(_a),
        vaccinated(false), rng(_d), infectts(SimTime::MAX_TIME), istate() {}
    public: Person( const Person &p ) :
        personid(p.personid), createdat(p.createdat), age(p.age),
        vaccinated(p.vaccinated), rng(p.rng), infectts(p.infectts),
        istate(p.istate) {}
    public: const Person &operator=( const Person &p )
        { personid=p.personid; createdat=p.createdat; age=p.age;
        vaccinated=p.vaccinated; rng=p.rng; infectts=p.infectts;
        istate=p.istate; return *this; }
    public: virtual ~Person() {}

    public: void setinfectts(const SimTime &ts){ infectts = ts; }
    public: const SimTime &getinfectts(void)const{ return infectts; }

    public: void markinfectious( void )
                 { istate.resetto(INFECTIOUS); }
    public: bool isinfectious( void )const
                 { return istate.isset(INFECTIOUS); }

    public: const PersonID &getpersonid(void)const{return personid;}
    public: const SimPID &getcreatedat(void)const{return createdat;}
    public: const double &getage(void)const{return age;}

    public: void setvaccinated( void ){ vaccinated = true; }
    public: bool isvaccinated( void )const{ return vaccinated; }

    public: void setrng( double _r ) { rng = _r; }
    public: double getrng( void )const{ return rng; }

    public: InfectionState &accistate(void){return istate;}
    public: const InfectionState &getistate(void)const{return istate;}

    /*These remain constant/unmodified after creation*/
    private: PersonID personid;
    private: SimPID createdat;
    private: double age;
    private: bool vaccinated;
    private: double rng;

    /*These are updated over time*/
    private: SimTime infectts; //TS of next event in infection chain, if started
    private: InfectionState istate; //Current infection state

    public: ostream &operator>>( ostream &out ) const
                  { return out <<"{"<<createdat<<"."<<personid<<" "
                               <<(isvaccinated()?"V":"")
                               <<(isinfectious()?"I":"") <<" "
                               <<rng<<" NTS@"<<infectts
                               <<" IS"<<istate.get()<<"}"; }
};
ostream &operator<<(ostream &out, const Person &p) { return p>>out; }

//-----------------------------------------------------------------------------
class PersonContainer
{
    public: PersonContainer( const Person &p, const SimTime &dts ) :
              person(p), departurets(dts) {}
    public: PersonContainer( const PersonContainer &c ) :
              person(c.person), departurets(c.departurets) {}
    public: virtual ~PersonContainer() {}

    public: Person &accperson( void ) { return person; }
    public: const Person &getperson( void ) const { return person; }
    public: const SimTime &getdts( void ) const { return departurets; }

    private: Person person;
    private: SimTime departurets;
};

//-----------------------------------------------------------------------------
class Location : public NormalSimProcess
{
    public: Location( long pnum,
                      const string &lname, const string &jsfname,
                      const HealthTransition &pttsnorm,
                      const HealthTransition &pttsvacc );
    protected: virtual void init( void );
    protected: virtual void execute( SimEvent *event );
    protected: virtual void wrapup( void );

    protected: Region *psim(){return (Region*)Simulator::sim();}

    protected: long locnum;
    protected: string locname;
    protected: long initialpop;
    protected: long nsent, nrecd, ninfected;
    protected: typedef map<Person::PersonID, PersonContainer> OccupantMap;
    protected: OccupantMap occupants;
    protected: unsigned long tempid_counter;/*For unique temp IDs of occupants*/
    protected: struct { HealthTransition ptts; } normal, vaccinated;

    protected: virtual void commit_event( SimEventBase *e, bool is_kernel );

    protected: void evolve_infection( Person &person,
                                      const SimTime &dts, int tempid );
    protected: int infect_occupants( const Person::PersonID &tempid );

    protected: double infectprob; void recompute_infectprob( void ); //XXX

    protected: double randunif(void) { return RandUnif(PID().loc_id); }
    protected: void revrandunif(void) { RandReverseUnif(PID().loc_id); }
    protected: double randunif( double high, double low = 0.0 )
                   { return low + (randunif()*(high-low)); }
    protected: double randexp( double mean )
                   { return RandExponential(PID().loc_id, mean); }
};

//-----------------------------------------------------------------------------
class Collector : public NormalSimProcess
{
    public: Collector( void );
    public: virtual ~Collector() {}
    protected: virtual void init( void );
    protected: virtual void execute( SimEvent *event );
    protected: virtual void wrapup( void );

    protected: Region *psim(){return (Region*)Simulator::sim();}

    protected: struct SimPIDCmp {
                   bool operator()( const SimPID &s1, const SimPID &s2 ) const
                       { return s1.fed_id < s2.fed_id ||
                                (s1.fed_id == s2.fed_id &&
                                 s1.loc_id < s2.loc_id); }
               };
    protected: typedef map<SimPID,long,SimPIDCmp> CounterMap;

    protected: CounterMap ninfected;
    protected: long totinfected;
    protected: void compute_total( void );
};

//-----------------------------------------------------------------------------
class Region : public Simulator
{
    public: Region( void );
    public: virtual void init( int ac, char *av[] );
    public: virtual void run( void );
    public: virtual void stop( void );

    public: const string &regionname( void ) const { return regname; }
    public: long getnlocations( void ) const { return nlocations; }
    public: long getnpersons( void ) const { return npersons; }
    public: const SimTime &getlatu( void ) const { return latu; }
    public: const SimTime &getendtu( void ) const { return endtu; }
    public: const SimPID &getcollectorpid( void ) const { return collector_pid;}

    protected: string regname;
    protected: SimPID collector_pid;
    protected: long nlocations, npersons;
    protected: SimTime latu;/*Lookahead in timeunits*/
    protected: SimTime endtu;

    /*Time-unit conversions*/
    public: static double DAYS2TU(const double &days){ return days*24.0; }
    public: static double HRS2TU(const double &hrs){ return hrs; }
    public: static double SECS2TU(const double &secs){ return secs/3600.0;}
    public: static double MINS2TU(const double &mins){ return mins/60.0;}

    protected: void randinit(int n) { RandInit(n, fed_id()); }

    public:  struct Stats
             { unsigned long nsent, nrecd; Stats() { nsent = nrecd = 0; }
             } local, remote, nundone, tot;
    public:  struct Probs
             { double infected; //Frac of initially infected
               double vaccinated; //Frac of initially vaccinated
               double meanstaydt; //Avg #hrs person stays at a location
               double meanlocaltraveldt; //Avg travel time b/w local locations
               double meanremotetraveldt;//Avg travel time b/w remote locations
               double locality; //Percent of events staying within same federate
               long nbrreach; //#locations +/- mylocnum to choose as a dest
              Probs() {
               infected = 0.01;
               vaccinated = 0.002;
               meanstaydt = Region::HRS2TU(1.0);
               meanlocaltraveldt = Region::HRS2TU(0.5);
               meanremotetraveldt = Region::HRS2TU(2.0);
               locality = 0.90;
               nbrreach = -1;
              }
             };
    protected:  Probs prob;
    public:  const Probs &getprob( void ) const { return prob; }
};

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
enum RedifEventType { ARRIVAL, DEPARTURE, ISTATECHANGE, STATS };
struct RedifData
{
    RedifEventType etype;
    int ninfected;
};
class RedifEvent : public SimEvent
{
    DEFINE_BASE_EVENT(Redif, RedifEvent, SimEvent);
    public: RedifEvent( const RedifEventType &et )
            { rdata.etype = et; rdata.ninfected = 0; }
    public: const RedifEventType &getetype( void ) const { return rdata.etype; }
    public: RedifData rdata;
};

//-----------------------------------------------------------------------------
struct ArrivalData
{
    Person person;
};
class ArrivalEvent : public RedifEvent
{
    DEFINE_LEAF_EVENT(Arrival, ArrivalEvent, RedifEvent);
    public: ArrivalEvent(void) :
            RedifEvent(ARRIVAL) {}
    public: ArrivalEvent( const Person &p ) :
            RedifEvent(ARRIVAL) { data.person = p; }

    public: ArrivalData data;
};

//-----------------------------------------------------------------------------
struct DepartureData
{
    Person::PersonID tempid;
};
class DepartureEvent : public RedifEvent
{
    DEFINE_LEAF_EVENT(Departure, DepartureEvent, RedifEvent);
    public: DepartureEvent( void ) :
            RedifEvent(DEPARTURE) {}
    public: DepartureEvent( const Person::PersonID &id ) :
            RedifEvent(DEPARTURE) { data.tempid = id; }

    public: DepartureData data;
};

//-----------------------------------------------------------------------------
struct InfectionStateChangeData
{
    Person::PersonID tempid;
    int saved_istate;
    SimTime saved_infectts;
};
class InfectionStateChangeEvent : public RedifEvent
{
    DEFINE_LEAF_EVENT(InfectionStateChange,
            InfectionStateChangeEvent, RedifEvent);
    public: InfectionStateChangeEvent( void ) :
            RedifEvent(ISTATECHANGE)
            { data.saved_infectts = SimTime::MAX_TIME; }
    public: InfectionStateChangeEvent( const Person::PersonID &id ) :
            RedifEvent(ISTATECHANGE)
            { data.tempid = id; data.saved_infectts = SimTime::MAX_TIME; }
    public: InfectionStateChangeData data;
};

//-----------------------------------------------------------------------------
struct StatisticsData
{
    int ninfected;
};
class StatisticsEvent : public RedifEvent
{
    DEFINE_LEAF_EVENT(Statistics, StatisticsEvent, RedifEvent);
    public: StatisticsEvent( void ) : RedifEvent(STATS) { data.ninfected = 0; }
    public: StatisticsData data;
};
//-----------------------------------------------------------------------------
struct EventData
{
    RedifData redif;
    union
    {
        ArrivalData arr;
        DepartureData dep;
        InfectionStateChangeData isc;
        StatisticsData stat;
    };
};

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void HealthTransition::loadptts( json &js )
{
    const json &states = js["states"];
    EXADBG(0,"PTTS #states " << states.size());
    for(auto &st : states.items())
    {
        const string &stname = st.key();
        int stnum = st.value();
        setstatename( stnum, stname );
        EXADBG(0, "State[\"" << stname << "\"] -> "<<stnum);
    }

    const json &transitions = js["transitions"];
    for(auto &tr : transitions.items())
    {
        const string &fromstname = tr.key();
        const json &tostates = tr.value();
        int fromstnum = states[fromstname];
        EXADBG( 0, "Transition[" << fromstname << "(" << fromstnum << ")]->" );
        for(auto &trto : tostates.items())
        {
            const string &tostname = trto.key();
            const json &tostvals = trto.value();
            int tostnum = states[tostname];
            vector<double> params;
            tostvals.get_to(params);
            ENSURE( 0, params.size() == 3, params.size() );
            double prob = params[0];
            double dwelllo = params[1];
            double dwellhi = params[2];
            EXADBG( 0, "\t" << tostname << "(" << tostnum << ")" << params.size()
                       << " prob= " << prob << " dwell=[" << dwelllo << ", "
                       << dwellhi << "]" );
            addtransition( fromstnum, tostnum, prob,
                           Region::DAYS2TU(dwelllo), Region::DAYS2TU(dwellhi) );
        }
    }

    ENSURE( 0, verify(), "" );
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
Collector::Collector( void ) : ninfected(), totinfected(0)
{
    Region *reg = psim();
    enable_undo( false );
    add_dest( SimPID::ANY_PID, SimTime::MAX_TIME );
}

//-----------------------------------------------------------------------------
void Collector::init( void )
{
}

//-----------------------------------------------------------------------------
void Collector::compute_total( void )
{
    long old = totinfected;
    totinfected = 0;
    for( CounterMap::const_iterator it = ninfected.begin();
         it != ninfected.end(); it++ )
    {
        totinfected += it->second;
    }
    ENSURE( 0, old <= totinfected, old<<" "<<totinfected );
}

//-----------------------------------------------------------------------------
void Collector::execute( SimEvent *event )
{
    Region *reg = psim();
    RedifEvent *re = reinterpret_cast<RedifEvent *>(event);
    ENSURE( 0, re->getetype() == STATS, "" );
    StatisticsEvent *se = reinterpret_cast<StatisticsEvent *>(re);
    ninfected[event->source()] = se->data.ninfected;
    compute_total();
    EXADBG( 0, PID() << " @ " << now().ts <<
            " EVENTNAME= " << *event->name() <<
            " LOCATION= " << event->source() <<
            " NINFECTED " << se->data.ninfected <<
            " TOTINFECTED " << totinfected );
}

//-----------------------------------------------------------------------------
void Collector::wrapup( void )
{
    compute_total();
    EXADBG( 0, PID() << " TOTINFECTED " << totinfected );
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
Location::Location( long pnum,
                    const string &lname, const string &jsfname,
                    const HealthTransition &pttsnorm,
                    const HealthTransition &pttsvacc ) :
    locnum(pnum), locname(lname), nsent(0), nrecd(0), ninfected(0),
    occupants(), tempid_counter(0)
{
    Region *reg = psim();
    enable_undo( false, 10*reg->getlatu(), 0 );
    add_dest( SimPID::ANY_PID, reg->getlatu() );

    normal.ptts = pttsnorm;
    vaccinated.ptts = pttsvacc;

    {
        EXADBG(0,"Location reading file "<<jsfname);
        json js; ifstream ifs( jsfname ); ifs >> js;
        EXADBG(0,"Location done reading file");
        const string &popstr = js["population"];
        initialpop = string2int(popstr);
        EXADBG(0,locname << " initial population " << initialpop);
        initialpop = initialpop / gconfig.scaledown;
        EXADBG(0,locname << " scaled-down initial population " << initialpop);
    }
}

//-----------------------------------------------------------------------------
void Location::init( void )
{
    Region *reg = psim();

    /*Add persons to this location*/
    long npersons2create = initialpop;
    Person::PersonID startpid = 0; //XXX TBC
    EXADBG(3, PID()<<" startpersonid "<<startpid);
    int ninf = 0;
    for( long i = 0; i < npersons2create; i++ )
    {
        Person newp( startpid+i, PID(), randunif(), randunif() );

        SimTime arrdt = reg->getlatu() + Region::HRS2TU(randunif());

        if(randunif() < reg->getprob().infected)
        {
            SimTime infectdt = reg->getlatu() +
                               Region::HRS2TU(randexp(0.1));//XXX CUSTOMIZE
            SimTime infectts = arrdt+infectdt;
            newp.markinfectious();
            newp.setinfectts(infectts);
            ninf++;
        }

        if(randunif() < reg->getprob().vaccinated)
        {
            newp.setvaccinated();
        }

        ArrivalEvent *ae = new ArrivalEvent( newp );
        send( PID(), ae, arrdt );
        nsent++;

        EXADBG(3,PID()<<" created "<<newp<<" dt="<<arrdt);

        ANIMT("SCE "<<newp.getpersonid()<<" "<<newp.getistate().get());
    }

    EXADBG(0,PID()<<" INITIAL NINFECTED "<<ninf);
}

//-----------------------------------------------------------------------------
void Location::evolve_infection( Person &person, const SimTime &dts, int tempid)
{
    const HealthTransition &trans =
            (person.isvaccinated() ?  vaccinated.ptts : normal.ptts);
    int ist = person.getistate().get();
    double rng = randunif();
    const HealthTransition::Entry &entry = trans.nextstate( ist, rng );

    person.accistate().resetto( entry.j );

    /*Schedule its next infection state change, if any*/
    double lo = entry.lo.dwelltime, hi = entry.hi.dwelltime;
    if( lo>0 || hi>0 )
    {
        SimTime infectdt = randunif( hi, lo );
        SimTime infectts = now()+infectdt;
        person.setinfectts( infectts );
        if( infectts <= dts )
        {
            InfectionStateChangeEvent *new_ie =
                new InfectionStateChangeEvent( tempid );
            send( PID(), new_ie, infectdt );
        }
    }
}

//-----------------------------------------------------------------------------
void Location::recompute_infectprob( void )
{
    int N = 0;
    double overlapdt = 0;
    for( OccupantMap::const_iterator it = occupants.begin();
         it != occupants.end(); it++ )
    {
        const PersonContainer &container = it->second;
        const Person &person = container.getperson();
        if( person.isinfectious() )
        {
            SimTime dt = container.getdts() - now(); //XXX arrival_ts or now()?
            overlapdt += dt.ts;
            N++;
        }
    }

    double avgdt = ( (N <= 1) ? 0.0 : (overlapdt / (N-1)) );
    const double r = 0.3, s = 0.05, rho = 0.05;
    infectprob = 1 - exp( N * avgdt * log(1 - (r*s*rho)) );
    ENSURE( 0, 0.0 <= infectprob && infectprob <= 1.0, infectprob );

    EXADBG( 3, "N= "<<N<<" overlapdt= "<<avgdt<<" infectprob= "<<infectprob );
}

//-----------------------------------------------------------------------------
int Location::infect_occupants( const Person::PersonID &tempid )
{
    Region *reg = psim();
    int ninf = 0;
    OccupantMap::iterator it = occupants.find(tempid);
    ENSURE( 0, it != occupants.end(), "" );
    PersonContainer &container = it->second;
    Person &arrperson = container.accperson();
    if( !arrperson.isinfectious() )
    {
        if( arrperson.getistate().get()==UNINFECTED &&
            arrperson.getrng() <= infectprob )
        {
            ENSURE( 0, arrperson.getinfectts() >= SimTime::MAX_TIME, arrperson);

            evolve_infection( arrperson, container.getdts(), tempid );
            ninf++;

            EXADBG( 1, PID()<<" @ "<<now()<<" INFECTION of "<<
                       arrperson<<" upon its arrival" );
        }
    }
    else
    {
      recompute_infectprob();

      for( OccupantMap::iterator it=occupants.begin();
         it != occupants.end(); it++ )
      {
        PersonContainer &container = it->second;
        Person &person = container.accperson();
        if( person.getistate().get()==UNINFECTED &&
            person.getrng() <= infectprob )
        {
            ENSURE( 0, person.getinfectts() >= SimTime::MAX_TIME, person );

            int tempid = it->first;
            evolve_infection( person, container.getdts(), tempid );
            ninf++;

            ANIMT("SCE "<<person.getpersonid()<<" "<<person.getistate().get());

            EXADBG( 1, PID()<<" @ "<<now()<<" INFECTION of "<<
                       person<<" upon arrival of "<<arrperson );
        }
      }
    }

    EXADBG( 2, PID()<<" @ "<<now()<<" infect_occupants()= " << ninf);

    if( ninf > 0 )
    {
        long div = (reg->getnpersons()/(reg->getnlocations()))/10;
        if( false /*XXX TBC*/ && div > 0 && (ninfected+ninf) % div == 0 )
        {
            StatisticsEvent *se = new StatisticsEvent();
            se->data.ninfected = ninfected+ninf;
            send( reg->getcollectorpid(), se, reg->getlatu() );
        }
    }

    return ninf;
}

//-----------------------------------------------------------------------------
void Location::execute( SimEvent *event )
{
    Region *reg = psim();
    RedifEvent *re = reinterpret_cast<RedifEvent *>(event);

    nrecd++;

    EXADBG( 2, "Location " << PID() << " @ " << now() <<
               " execute " << *re << " eventtype=" << re->getetype() <<
               " nsent= " << nsent << " recd= " << nrecd );

if(re->source().fed_id!=PID().fed_id){
  if(reg->remote.nrecd++%100000==99999){EXADBG(0,"Fed "<<PID().fed_id<<" remote-nrecd="<<reg->remote.nrecd);}
}else{
  if(reg->local.nrecd++%1000000==999999){EXADBG(0,"Fed "<<PID().fed_id<<" local-nrecd="<<reg->local.nrecd);}
}

    switch( re->getetype() )
    {
        case ARRIVAL:
        {
            ArrivalEvent *ae = reinterpret_cast<ArrivalEvent *>(re);

            EXADBG( 2, PID()<<" @ "<<now()<<" ARRIVAL of "<<
                       ae->data.person<<" from "<<ae->source() );

            double staydt = randexp( reg->getprob().meanstaydt );
            SimTime depdt = reg->getlatu() + staydt;
            SimTime depts = now() + depdt;

            /*Generate a locally unique, temporary identifier*/
            Person::PersonID tempid = tempid_counter++;

            /*Add to local occupants*/
            PersonContainer container( ae->data.person, depts );
            const Person &person = container.getperson();
            ENSURE( 0, occupants.find(tempid) == occupants.end(), "Duplicate?");
            occupants.insert( OccupantMap::value_type( tempid, container ) );

            /*Schedule its departure*/
            DepartureEvent *de = new DepartureEvent( tempid );
            send( PID(), de, depdt );

            re->rdata.ninfected = infect_occupants( tempid );

            EXADBG( 2, PID()<<" @ "<<now()<<" DEPARTURE of "<<
                       person<<" at "<<depts );

            break;
        }
        case DEPARTURE:
        {
            DepartureEvent *de = reinterpret_cast<DepartureEvent *>(re);

            /*Locate the one to depart*/
            OccupantMap::const_iterator occ_it = occupants.find(de->data.tempid);
            ENSURE( 0, occ_it != occupants.end(), "Must be an occupant" );
            const PersonContainer &container = occ_it->second;
            const Person &person = container.getperson();

            /*Select a random location and travel time*/
            long destlid = 0, destfid = 0;
            SimTime arrdt = 0;
            long destloc = 0;
            if( 1 || randunif() < reg->getprob().locality ) //XXX
            {
                destlid = 0; //XXX
                destfid = 0;
                destloc = destlid;
                arrdt = randexp( reg->getprob().meanlocaltraveldt );
            }
            else
            {
                long NL = reg->getnlocations();
                long R = reg->getprob().nbrreach;
                if( R < 0 )
                {
                    destloc = randunif()*NL;
                }
                else
                {
                    long locoffset = long(randunif()*2*R);
                    destloc = (locnum-R+locoffset);
                    while( destloc < 0 ) { destloc += NL; }
                    destloc %= NL;
                }
                destlid = 0;
                destfid = 0;
                arrdt = randexp( reg->getprob().meanremotetraveldt );
            }
            arrdt += reg->getlatu();
            SimPID dest( destlid, destfid );

            /*Send it out*/
            ArrivalEvent *ae = new ArrivalEvent( person );
            send( dest, ae, arrdt );

            ANIMT("DE "<<person.getpersonid()<<" "<<locnum<<
                  " "<<destloc<<" "<<arrdt.ts);

            occupants.erase( de->data.tempid );

            EXADBG( 2, PID()<<" @ "<<now()<<" DEPARTURE of "<<
                       ae->data.person<<" to "<<dest );
            break;
        }
        case ISTATECHANGE:
        {
            InfectionStateChangeEvent *ie =
                    reinterpret_cast<InfectionStateChangeEvent *>(re);

            /*Locate the occupant*/
            OccupantMap::iterator occ_it = occupants.find(ie->data.tempid);
            ENSURE( 0, occ_it != occupants.end(), "Must be an occupant" );
            ENSURE( 0, occ_it->first == ie->data.tempid, "Just checking" );
            PersonContainer &container = occ_it->second;
            Person &person = container.accperson();

            /*Save current state & timestamp*/
            ie->data.saved_istate = person.getistate().get();
            ie->data.saved_infectts = person.getinfectts();

            /*Move to its next state*/
            evolve_infection( person, container.getdts(), ie->data.tempid );

            ANIMT("SCE "<<person.getpersonid()<<" "<<person.getistate().get());

            /*If this became infectious, determine effect on others*/
            re->rdata.ninfected = infect_occupants( ie->data.tempid );

            break;
        }
        default:
        {
            FAIL("Impossible");
            break;
        }
    }
    nsent++;
}

//-----------------------------------------------------------------------------
void Location::commit_event( SimEventBase *event, bool is_kernel)
{
    if( !is_kernel )
    {
        RedifEvent *re = (RedifEvent *)event;
        if( re->rdata.ninfected > 0 )
        {
            ninfected += re->rdata.ninfected;
            EXADBG( 0, PID() << " @ " << now().ts <<
                   " EVENTNAME= " << *event->name() <<
                   " NINFECTED " << re->rdata.ninfected <<
                   " TOTINFECTED " << ninfected );
        }
    }

    NormalSimProcess::commit_event( event, is_kernel );
}

//-----------------------------------------------------------------------------
void Location::wrapup( void )
{
    Region *reg = psim();
    EXADBG( 2, "Location " << PID() <<
               " ninfected= " << ninfected <<
               " nsent= " << nsent << " nrecd= "<<nrecd );
    reg->tot.nsent += nsent;
    reg->tot.nrecd += nrecd;
}

/*---------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/
Region::Region( void ) :
    regname(""), nlocations(2), npersons(10), endtu(360)
{
    latu = HRS2TU(0.1); //XXX CUSTOMIZE
}

/*---------------------------------------------------------------------------*/
void Region::init( int ac, char *av[] )
{
    EXADBG( 2, "------------------ Staring up ---------------" );
    bool redirect = !getenv("REDIRECT") || strcmp(getenv("REDIRECT"),"false");
    Simulator::init(redirect?"stdout":0);
    EXADBG( 2, "------------------ Federation synchronized ---------------" );

    {
        static ofstream exadbgstrm;
        string logfname = "exacorona-" + to_string(fed_id()) + ".log";
        exadbgstrm.open(logfname);
        _exadbgstrm = &exadbgstrm;
        cout<<"Writing ExaCorona log to "<<logfname<<endl;
    }

    //Disease
    HealthTransition pttsnorm(gconfig.pttsnorm.filename);
    HealthTransition pttsvacc(gconfig.pttsvacc.filename);

    //Geography
    {
        string geofname = gconfig.geo.filename;
        json js; ifstream infs(geofname); infs >> js;
        EXADBG(0,geofname<<" read");
        vector<json> activeregions = js["active regions"];
        EXADBG(0,"activeregions="<<activeregions);
        EXADBG(0,"activeregions="<<activeregions.size());
        int myregnum = fed_id();
        EXADBG(0,"regnum="<<myregnum);
        json myregionjson = activeregions[myregnum];
        EXADBG(0,"region="<<myregionjson);
        regname = myregionjson["name"];
        EXADBG(0,"regname="<<regname);
        string myregfile = gconfig.prefdir(myregionjson["file"]);
        EXADBG(0,"json#myregfile="<<myregfile);

        //My region
        {
            json regjs; ifstream reginfs(myregfile); reginfs >> regjs;

            //Named locations
            {
                vector<json> locdet = regjs["locations from files"];
                EXADBG(0,"locdet="<<locdet);
                long loci = 0; //XXX TBC
                for( auto &ldi : locdet )
                {
                    EXADBG(0,ldi);
                    string locname = ldi["name"];
                    string locfile = gconfig.prefdir(ldi["file"]);
                    long locid = loci++;
                    Location *location = new Location(locid, locname, locfile,
                                                      pttsnorm, pttsvacc);
                    add( location );
                    EXADBG( 0, "Added location " << locid << " ID= " <<
                            location->PID() << " " << locname << " " << locfile );
                }
            }

            //Anonymous locations drawn from a distribution
            {
                json locdist = regjs["locations from distribution"];
                EXADBG(0,"locdist="<<locdist);
                EXADBG(0,"TO BE COMPLETED");
            }

            //Locations from mesh creation
            {
                json locmesh = regjs["locations from mesh"];
                EXADBG(0,"locmesh="<<locmesh);
                EXADBG(0,"TO BE COMPLETED");
            }
        }
    }

    nlocations = 10;
    npersons = 100;
    endtu.ts = 10.0;

    const char *envstr = 0;
    if(envstr=getenv("EXACORONA_NBRREACH")) prob.nbrreach = atoi(envstr);
    SIMCFG( "EXACORONA_NBRREACH", prob.nbrreach, "+/-Neighbor Reach" );
    ENSURE( 0, prob.nbrreach < 0 || prob.nbrreach <= nlocations,
            "nbrreach " << prob.nbrreach << " should be -1 or <="<<nlocations );

    ENSURE( 0, 0.0 <= prob.locality && prob.locality <= 1.0,
            "Locality " << prob.locality << " should be percentage [0..100]" );

    if(fed_id()==0)
    {
        cout << fed_id() << ": Region" <<
               " lookahead= " << latu << " hours" <<
               " locality= " << prob.locality*100.0<<"%" <<
               " #locations= " << nlocations <<
               " #persons= " << npersons <<
               " endtime= " << endtu << " hours" <<
               endl;
    }

    long locations_per_fed = nlocations/num_feds();
    
    randinit(locations_per_fed);
    if(fed_id()==0)EXADBG( 0, "RNG streams initialized." );

    if( fed_id() == 0 )
    {
        Collector *collector = new Collector();
        add( collector );
    }

    collector_pid = SimPID( locations_per_fed, 0 ); //Last one on 0'th fed

    if( fed_id() == 0 )
    {
        ANIM( "-1 N "<<num_feds()<<" "<<getnlocations()<<" "<< getnpersons() );
    }
}

/*---------------------------------------------------------------------------*/
void Region::run( void )
{
    if(fed_id()==0)
    {
        cout << "\n---------\nStarting ExaCorona: "<<
               " on "<<num_feds() << " feds\n---------" << endl;
    }

    report_status( endtu/10, endtu );

    for( Simulator::start(); Simulator::run(endtu) < endtu; ) {}

    if(fed_id()==0)
    {
        cout << "\n---------\nStopping ExaCorona: "<<
               " on "<<num_feds() << " feds\n---------" << endl;
    }
}

/*---------------------------------------------------------------------------*/
void Region::stop( void )
{
    Simulator::stop();

    if( fed_id() == 0 )
    {
    cout<< fed_id()<<": #Arrivals sent= " << tot.nsent
        << ", " << " recd= " << tot.nrecd << " undone= "
        << nundone.nrecd << endl;
    }
}

/*---------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/
int app_event_type( const SimEvent *ev )
{
    RedifEvent *re = (RedifEvent *)ev;
    EXADBG( 8, "app_event_event_type " << re->getetype() );
    return re->getetype();
}

/*---------------------------------------------------------------------------*/
int app_event_data_size( int evtype, const SimEvent *ev )
{
    EXADBG( 8, "app_event_data_size " << sizeof(EventData) );
    return sizeof(EventData);
}

/*---------------------------------------------------------------------------*/
SimEvent *app_event_create( int evtype, char *buf )
{
    EXADBG( 8, "starting app_event_create " << evtype );
    SimEvent *ev = 0;
    switch( evtype )
    {
        case ARRIVAL: 
        {
            ArrivalEvent *ae = new (buf) ArrivalEvent();
            ev = ae;
            break;
        }
        case DEPARTURE: 
        {
            DepartureEvent *de = new (buf) DepartureEvent();
            ev = de;
            break;
        }
        case ISTATECHANGE: 
        {
            InfectionStateChangeEvent *ie = new (buf) InfectionStateChangeEvent();
            ev = ie;
            break;
        }
        case STATS: 
        {
            StatisticsEvent *se = new (buf) StatisticsEvent();
            ev = se;
            break;
        }
        default:
        {
            FAIL("Impossible");
            break;
        }
    }
    EXADBG( 8, "done app_event_create " << evtype );
    return ev;
}

/*---------------------------------------------------------------------------*/
void app_event_data_pack( int evtype, const SimEvent *ev, char *buf, int bufsz )
{
    EXADBG( 8, "starting app_event_data_pack " << evtype );
    ENSURE( 0, bufsz == sizeof(EventData), bufsz << " " << sizeof(EventData) );
    EventData *ed = reinterpret_cast<EventData *>(buf);
    switch( evtype )
    {
        case ARRIVAL: 
        {
            const ArrivalEvent *ae = reinterpret_cast<const ArrivalEvent*>(ev);
            ed->redif = ae->rdata;
            ed->arr = ae->data;
            break;
        }
        case DEPARTURE: 
        {
            const DepartureEvent *de = reinterpret_cast<const DepartureEvent*>(ev);
            ed->redif = de->rdata;
            ed->dep = de->data;
            break;
        }
        case ISTATECHANGE: 
        {
            const InfectionStateChangeEvent *ie =
                    reinterpret_cast<const InfectionStateChangeEvent*>(ev);
            ed->redif = ie->rdata;
            ed->isc = ie->data;
            break;
        }
        case STATS: 
        {
            const StatisticsEvent *se = reinterpret_cast<const StatisticsEvent*>(ev);
            ed->redif = se->rdata;
            ed->stat = se->data;
            break;
        }
        default:
        {
            FAIL("Impossible");
            break;
        }
    }
    EXADBG( 8, "done app_event_data_pack " << evtype );
}

/*---------------------------------------------------------------------------*/
void app_event_data_unpack( int evtype, SimEvent *ev, const char *buf, int bufsz )
{
    EXADBG( 8, "starting app_event_data_unpack " << evtype );
    ENSURE( 0, bufsz == sizeof(EventData), bufsz << " " << sizeof(EventData) );
    const EventData *ed = reinterpret_cast<const EventData *>(buf);
    switch( evtype )
    {
        case ARRIVAL: 
        {
            ArrivalEvent *ae = reinterpret_cast<ArrivalEvent*>(ev);
            ae->rdata = ed->redif;
            ae->data = ed->arr;
            break;
        }
        case DEPARTURE: 
        {
            DepartureEvent *de = reinterpret_cast<DepartureEvent*>(ev);
            de->rdata = ed->redif;
            de->data = ed->dep;
            break;
        }
        case ISTATECHANGE: 
        {
            InfectionStateChangeEvent *ie =
                    reinterpret_cast<InfectionStateChangeEvent*>(ev);
            ie->rdata = ed->redif;
            ie->data = ed->isc;
            break;
        }
        case STATS: 
        {
            StatisticsEvent *se = reinterpret_cast<StatisticsEvent*>(ev);
            se->rdata = ed->redif;
            se->data = ed->stat;
            break;
        }
        default:
        {
            FAIL("Impossible");
            break;
        }
    }
    EXADBG( 8, "done app_event_data_unpack " << evtype );
}

/*---------------------------------------------------------------------------*/
void GlobalConfig::load( json &js )
{
    gconfig.scaledown         = js["scaledown factor"];
    gconfig.endtime           = js["endtime"];
    gconfig.lookahead         = js["lookahead"];
    gconfig.scenariodir       = js["scenario"];

    {
        string sfname = prefdir("scenario.json");
        json sjs; ifstream infs(sfname); infs >> sjs;

        gconfig.geo.filename      = prefdir(sjs["geography"]);
        gconfig.mob.filename      = prefdir(sjs["mobility"]);
        gconfig.pttsnorm.filename = prefdir(sjs["disease-normal"]);
        gconfig.pttsvacc.filename = prefdir(sjs["disease-vaccinated"]);
    }
}

/*---------------------------------------------------------------------------*/
int main( int ac, char *av[] )
{
    {
        string gconfigfname = "exacorona.json";
        if( ac > 1 )
        {
            gconfigfname = av[1];
        }
        cout<<"Configuration: \"" << gconfigfname<<"\""<<endl;
        json js; ifstream infs(gconfigfname);
        if(!infs.is_open())
        {
            cerr<<"Need \""<<gconfigfname<<"\"...exiting."<<endl;
            exit(1);
        }
        infs >> js;
        gconfig.load( js );
    }

    Region reg;
    reg.pre_init( &ac, &av );
    reg.init( ac, av );
    reg.run();
    reg.stop();
    return 0;
}

/*---------------------------------------------------------------------------*/