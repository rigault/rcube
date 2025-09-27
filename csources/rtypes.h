/* this file includes in other file provides
. Constant definition with #define and enum
. Type definitions with typedef
*/

#define N_METEO_CONSULT_WIND_URL    6
#define N_METEO_CONSULT_CURRENT_URL 6
#define METEO_CONSULT_WIND_DELAY    5  // nb hours after time run to get new grib
#define METEO_CONSULT_CURRENT_DELAY 12
#define NOAA_DELAY                  4
#define METEO_FRANCE_DELAY          6
#define ECMWF_DELAY                 8
#define METEO_CONSULT_ROOT_GRIB_URL "https://static1.mclcm.net/mc2020/int/cartes/marine/grib/"
#define NOAA_ROOT_GRIB_URL          "https://nomads.ncep.noaa.gov/cgi-bin/"
#define NOAA_GENERAL_PARAM_GRIB_URL "var_GUST=on&var_PRMSL=on&var_PRATE=on&var_UGRD=on&var_VGRD=on&lev_10_m_above_ground=on&lev_surface=on&lev_mean_sea_level=on"
#define ECMWF_ROOT_GRIB_URL         "https://data.ecmwf.int/forecasts/"
#define METEO_FRANCE_ROOT_GRIB_URL  "https://object.data.gouv.fr/meteofrance-pnt/pnt/"
#define CSV_SEP               ",;\t"            // general separators for CSV files
#define GPS_TIME_OUT          2000000           // 2 seconds
#define PROG_LOGO             "routing.png"     // logo file
#define PROG_WEB_SITE         "http://www.orange.com"  
#define MAX_N_DAYS_WEATHER    16                // Max number od days for weather forecast
#define MAX_N_POI             4096              // Max number of poi in poi file
#define MAX_SIZE_POI_NAME     64                // Max size of city name
#define MAX_SIZE_COUNTRY_CODE 3                 // Max size of country code
#define MAX_HISTORY_ROUTE     10                // Max number of history route stored
#define GPSD_TCP_PORT         "2947"            // TCP port for gps demon
#define N_MAIL_SERVICES       9                 // for mailServiceTab size
#define N_WEB_SERVICES        4                 // for service Tab size (NOAA, ECMWF, ARPEGE and AROME)
#define MAX_INDEX_ENTITY      512               // for shp. Index.

//#define MAX_SIZE_SHIP_NAME    21                // see AIS specificatiions

// NOAA or ECMWF or ARPEGE or AROME for web download or MAIL. Specific for current
enum {NOAA_WIND, ECMWF_WIND, ARPEGE_WIND, AROME_WIND, MAIL, MAIL_SAILDOCS_CURRENT}; 

enum {GPS_INDEX, AIS_INDEX};                    // for NMEA USB serial port reading
enum {POI_SEL, PORT_SEL};                       // for editor or spreadsheet, call either POI or PORT
enum {NO_COLOR, B_W, COLOR};                    // wind representation 
enum {NONE, ARROW, BARBULE};                    // wind representation 
enum {NOTHING, JUST_POINT, SEGMENT, BEZIER};    // bezier or segment representation
enum {UNVISIBLE, NORMAL, CAT, PORT, NEW};       // for POI point of interest
enum {GRIB_STOPPED = -2, GRIB_RUNNING = -1, GRIB_ERROR = 0, GRIB_OK = 1, GRIB_UNCOMPLETE = 2, GRIB_ONLY_DOWNLOAD = 3}; // for readGribCheck and readCurentGribCheck
enum {NO_ANIMATION, PLAY, LOOP};                // for animationActive status
enum {WIND_DISP, GUST_DISP, WAVE_DISP, RAIN_DISP, PRESSURE_DISP}; // for display

/*! for mail services */
enum {SAILDOCS_GFS, SAILDOCS_ECMWF, SAILDOCS_ICON, SAILDOCS_ARPEGE, SAILDOCS_AROME, SAILDOCS_CURR, MAILASAIL, NOT_MAIL}; 
// grib mail service providers
// GLOBAL_MARINET no longer supported

struct GribService { // NOAA_WIND, ECMWF_WIND, ARPEGE_WIND
   int  nShortNames;
   char warning [MAX_SIZE_LINE];
};

struct MailService { // different saildocs services and mailasail
   char address [MAX_SIZE_LINE];
   char libelle [MAX_SIZE_LINE];
   char service [MAX_SIZE_NAME];
   int  nShortNames;
   char suffix [MAX_SIZE_NAME];
   char warning [MAX_SIZE_LINE];
};

/*! Structure to store coordinates */
typedef struct {
    double x;
    double y;
} Coordinates;

/*! displayed zone */
typedef struct {
   unsigned int xL;
   unsigned int xR;
   unsigned int yB;
   unsigned int yT;
   double latMin;
   bool   anteMeridian;       // set at true if zone crosses meridian 180
   double latMax;
   double lonLeft;
   double lonRight;
   double latStep;
   double lonStep;
   double zoom;
} DispZone;

/*! Structure for geo map shputil */
typedef struct {
   Point  *points;
   double latMin;
   double latMax;
   double lonMin;
   double lonMax;
   int    numPoints;
   int    nSHPType;
   int    index [MAX_INDEX_ENTITY]; 
   int    maxIndex;
} Entity;

/*! for point of interest management */
typedef struct {
   double lat;
   double lon;
   int    type;
   int    level;
   char   cc [MAX_SIZE_COUNTRY_CODE];         // country code
   char   name [MAX_SIZE_POI_NAME];
} Poi;

/*! For GPS management */
/*typedef struct {
   double lat;
   double lon;
   double alt;
   char   uAlt;
   double cog;    // degrees
   double sog;    // Knots
   int    status;
   int    nSat;
   time_t time;   // epoch time
   bool   OK;
} MyGpsData;
*/
/*! value in NMEA AIS frame */
/*typedef struct {
   int mmsi;
   int messageId;
   double lat;
   double lon;
   double sog;
   int cog;
   char name [MAX_SIZE_SHIP_NAME];
   time_t lastUpdate;
   int minDist;      // evaluation in meters of min Distance to detect collision
} AisRecord;*/
