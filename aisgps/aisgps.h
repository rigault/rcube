#define MAX_SIZE_SHIP_NAME    21                // see AIS specificatiions
extern GHashTable *aisTable;

/*! For GPS management */
typedef struct {
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

/*! value in NMEA AIS frame */
typedef struct {
   int mmsi;
   int messageId;
   double lat;
   double lon;
   double sog;
   int cog;
   char name [MAX_SIZE_SHIP_NAME];
   time_t lastUpdate;
   int minDist;      // evaluation in meters of min Distance to detect collision
} AisRecord;

extern MyGpsData my_gps_data;
extern bool gpsToJson (char * str, size_t len);
extern bool aisTableInit (void);
extern bool testAisTable ();
extern int aisToStr (char *str, size_t maxLen);
extern int aisToJson (char *str, size_t maxLen);
extern char *nmeaInfo (char *strGps, size_t len);
extern void *getNmea (gpointer x);
