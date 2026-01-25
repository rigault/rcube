extern const  char *getCurrentDate (void);
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
extern void   handleFeedbackRequest (const char *fileName, const char *date, const char *clientIPAddress, const char *string);

extern ClientRequest clientReq;
extern const char *filter[]; 





