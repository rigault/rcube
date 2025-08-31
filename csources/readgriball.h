/*! grib data description */
extern FlowP *tGribData [];            // wind, current

extern char * gribReaderVersion (char *str, size_t maxLen);
extern bool readGribLists (const char *fileName, Zone *zone);
extern bool readGribParameters (const char *fileName, Zone *zone);
extern bool readGribAll (const char *fileName, Zone *zone, int iFlow);



