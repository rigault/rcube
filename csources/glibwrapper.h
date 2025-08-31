#include <stddef.h>
#include <string.h>
#include <math.h>
#define G_PI 3.14159265358979323846
#define MIN(i, j) (((i) < (j)) ? (i) : (j))
#define MAX(i, j) (((i) > (j)) ? (i) : (j))
#define CLAMP(x, lo, hi) ( ((x) < (lo)) ? (lo) : ( ((x) > (hi)) ? (hi) : (x)) )
#define g_strlcpy(dest, src, size) strlcpy((dest),(src),(size))
#define g_strlcat(dest, src, size) strlcat((dest),(src),(size))

#include <string.h>  /* strlen, memmove */
#include <stdbool.h>

/* mini_gstrsplit.h — drop-in sans GLib */
#pragma once
#include <stdlib.h>
#include <string.h>
#include <limits.h>

/*: Length of array of string NULL terminated (like GLib g_strv_length) */
static inline size_t g_strv_length (char * const *strv) {
   if (!strv) return 0;
   size_t n = 0; 
   while (strv[n]) n++; 
   return n;
}

/*! Free array with elements (like GLib g_strfreev) */
static inline void g_strfreev(char **strv) {
   if (!strv) return;
   for (char **p = strv; *p; ++p) free(*p);
   free(strv);
}

/*! Clone of GLib g_strsplit */
static inline char **g_strsplit(const char *string,
                                const char *delimiter,
                                int max_tokens) {
   if (!string) return NULL;
   size_t dlen = (delimiter && *delimiter) ? strlen(delimiter) : 0;

   if (*string == '\0') {
      char **v = malloc(sizeof *v);
      if (!v) return NULL;
      v[0] = NULL;
      return v;
   }

   // Empty delimitor or max_tokens == 1 => no split total sting is returned
   if (dlen == 0 || max_tokens == 1) {
      char **v = (char**)malloc(2 * sizeof *v);
      if (!v) return NULL;
      v[0] = strdup(string);
      if (!v[0]) { free(v); return NULL; }
      v[1] = NULL;
      return v;
   }

   if (max_tokens < 1) max_tokens = INT_MAX;

   size_t cap = 8, n = 0;
   char **v = (char**)malloc((cap + 1) * sizeof *v);
   if (!v) return NULL;

   const char *p = string;
   for (;;) {
      // If limit reached, last element is the reminder (including delimitors)
      if (n == (size_t)(max_tokens - 1)) {
         v[n] = strdup(p);
         if (!v[n]) goto oom;
         n++;
         break;
      }

      const char *q = strstr(p, delimiter);
      if (!q) { // last
         v[n] = strdup(p);
         if (!v[n]) goto oom;
         n++;
         break;
      }

      size_t len = (size_t)(q - p);               // can be 0 => empty element remain
      char *tok = (char*)malloc(len + 1);
      if (!tok) goto oom;
      memcpy(tok, p, len);
      tok[len] = '\0';
      v[n++] = tok;

      if (n + 1 > cap) {                          // +1 for final NULL
         cap *= 2;
         char **nv = (char**)realloc(v, (cap + 1) * sizeof *nv);
         if (!nv) goto oom;
         v = nv;
      }
      p = q + dlen;
   }
   v[n] = NULL;
   return v;

oom:
   for (size_t i = 0; i < n; i++) free(v[i]);
   free(v);
   return NULL;
}


/*! GLib LIKE ' ', '\t', '\n', '\v', '\f', '\r' */
static inline bool g_ascii_isspace (unsigned char c) {
   return c == ' ' || (c >= '\t' && c <= '\r'); // \t..\r include \t \n \v \f \r */
}

/*! Équivalent to GLIB g_strstrip: trim in  place, return  s */
static inline char *g_strstrip (char *s) {
   if (!s) return NULL;

    // skip leading
   char *start = s;
   while (*start && g_ascii_isspace ((unsigned char)*start)) start++;

   // find end (after skip)
   char *end = start + strlen(start);
   while (end > start && g_ascii_isspace((unsigned char)end[-1])) end--;

   // length after trim
   size_t len = (size_t)(end - start);

   if (start != s) memmove(s, start, len);
   s[len] = '\0';
   return s;
}

// Replace all 'delims' with  'repl' (g_strdelimit)
static inline void g_strdelimit (char *s, const char *delims, char repl) {
   if (!s || !delims) return;
   for (char *p = s; *p; ++p)
      if (strchr(delims, *p)) *p = repl;
}

/*! equivalent to GLIB */
static inline char *g_strdup(const char *s) {
   return s ? strdup(s) : NULL;
}

/*! close to g_path_get_basename */ 
static inline char *path_get_basename (const char *path) {
   if (!path) return NULL;
   if (*path == '\0') return strdup(".");

   const char *end = path + strlen(path);
   while (end > path && end[-1] == '/') end--;          // retire finals '/'
   if (end == path) return strdup("/");                 // "/" or "///" -> "/"

   const char *start = end;
   while (start > path && start[-1] != '/') start--;   

   size_t len = (size_t)(end - start);
   char *out = (char *)malloc(len + 1);
   if (!out) return NULL;
   memcpy(out, start, len);
   out[len] = '\0';
   return out;
}

/*! Equivalent to Glib g_str_has_suffix */ 
static inline bool g_str_has_suffix (const char *s, const char *suffix) {
   if (!s || !suffix) return false;
   size_t ls  = strlen(s);
   size_t lsu = strlen(suffix);
   if (lsu > ls) return false;
   // Case empty suffix -> true (GLib behavior)
   return memcmp(s + (ls - lsu), suffix, lsu) == 0;
}

/*! Equivalent to Glib g_str_has_prefix */ 
static inline bool g_str_has_prefix (const char *s, const char *prefix) {
   if (!s || !prefix) return false;
   size_t lp = strlen(prefix);
   return strncmp(s, prefix, lp) == 0;
}

/*! Attention NOT SAFE */ 
static inline int g_atomic_int_get (int *x) {
   return *x;
}

/*! Attention NOT SAFE */ 
static inline void g_atomic_int_set (int *x, int val) {
   *x = val;
}

