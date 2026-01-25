extern Pp        *isocArray;                    // two dimensions array for isochrones
extern IsoDesc   *isoDesc;                      // one dimension array for isochrones meta data
extern int       nIsoc;                         // number of isochrones calculated  by routing
extern int       maxNIsoc;                      // max number of Isoc considering Grib meta information and timestep
extern Pp        lastClosest;                   // closest point to destination in last isochrone computed
extern ChooseDeparture chooseDeparture;         // for choice of departure time
extern HistoryRouteList historyRoute;           // history of calculated routes
extern SailRoute route;                         // current route

extern bool    storeRoute (SailRoute *route, const Pp *pOr, const Pp *pDest);
extern bool    isoDescToStr (char *str, size_t maxLen);
extern bool    routeToStr (const SailRoute *route, char *str, size_t maxLen, char *footer, size_t maxLenFooter);
extern void    *routingLaunch ();
extern void    *bestTimeDeparture ();
extern void    *allCompetitors ();
extern void    freeHistoryRoute ();
extern void    logReport (int n);
extern void    saveRoute (SailRoute *route);
extern bool    exportRouteToGpx (const SailRoute *route, const char *filename);
extern bool    dumpIsocToFile (const char *fileName);
extern void    routingAtAngle (ClientRequest *clientReq, char *out, size_t maxLen);
extern void    routeAtAngle (ClientRequest *clientReq, char *out, size_t maxLen);
//extern GString *isochronesToJson ();
//extern GString *routeToJson (SailRoute *route, int index, bool isoc, bool isoDesc);
//extern GString *allCompetitorsToJson (int n, bool isoc, bool isoDesc);
//extern GString *bestTimeReportToJson (ChooseDeparture *chooseDeparture, bool isoc, bool isoDesc);




