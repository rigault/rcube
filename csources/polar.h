extern void    bestVmg (double tws, PolMat *mat, double *vmgAngle, double *vmgSpeed);
extern void    bestVmgBack (double tws, PolMat *mat, double *vmgAngle, double *vmgSpeed);
extern double  maxValInPol (const PolMat *mat);
extern bool    readPolar (bool check, const char *fileName, PolMat *mat, char *errMessage, size_t maxLen);
extern char    *polToStr (const PolMat *mat, char *str, size_t maxLen);
extern char    *polToStrJson (const char *fileName, const char *objName, char *out, size_t maxLen);
extern char    *sailLegendToStrJson (const char *sailName [], size_t len, char *out, size_t maxLen);



