extern const  char   *getCurrentDate ();
extern void   buildInitialOfShortNameList(const Zone *zone, char *str, size_t len);
extern bool   initContext (const char *parameterFileName, const char *pattern);
extern void   forbidToJson (char *res, size_t maxLen);
extern bool   decodeFormReq (const char *req, ClientRequest *clientReq);
extern void   infoCoordToJson (double lat, double lon, ClientRequest *clientReq, char *res, size_t maxLen);
extern bool   updateWindGrib (ClientRequest *clientReq, char *checkMessage, size_t maxLen);
extern bool   updateCurrentGrib (ClientRequest *clientReq, char *checkMessage, size_t maxLen);
extern bool   checkParamAndUpdate (ClientRequest *clientReq, char *checkMessage, size_t maxLen);
extern char   *routeToJson (SailRoute *route, bool isoc, bool isoDesc, char *res, size_t maxLen);
extern char   *nearestPortToStrJson (double lat, double lon, char *out, size_t maxLen);
extern char   *listDirToStrJson (char *root, char *dir, bool sortByName, const char *pattern, const char **filter, char *out, size_t maxLen);
extern void handleFeedbackRequest (const char *fileName, const char *date, const char *clientIPAddress, const char *string);

extern ClientRequest clientReq;
extern const char *filter[]; 

enum {REQ_KILL = -1793, REQ_TEST = 0, REQ_ROUTING = 1, REQ_COORD = 2, REQ_FORBID = 3, REQ_POLAR = 4, 
      REQ_GRIB = 5, REQ_DIR = 6, REQ_PAR_RAW = 7, REQ_PAR_JSON = 8, 
      REQ_INIT = 9, REQ_FEEDBACK = 10, REQ_DUMP_FILE = 11, REQ_NEAREST_PORT = 12, 
      REQ_MARKS = 13, REQ_CHECK_GRIB = 14, REQ_GPX_ROUTE = 15, REQ_GRIB_DUMP = 16}; // type of request




