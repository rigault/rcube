const MS_TO_KN = (3600.0/1852.0);   // conversion meter/second to knots

const sailLegend = {
  NA:     { bg: "black",  luminance: 0 },
  C0:     { bg: "green",  luminance: 85 },
  HG:     { bg: "purple", luminance: 48 },
  Jib:    { bg: "gray",   luminance: 128 },
  LG:     { bg: "blue",   luminance: 29 },
  LJ:     { bg: "yellow", luminance: 210 },
  Spi:    { bg: "orange", luminance: 170 },
  SS:     { bg: "red",    luminance: 76 }
};

/**
 * Returns a suitable text color ("black" or "white") for a given background luminance.
 * This ensures good readability based on the brightness of the background color.
 *
 * @param {number} luminance - A number between 0 (darkest) and 255 (brightest),luminance of the background color.
 * @returns {string} "white" if the background is dark, "black" otherwise.
 */
function getTextColorFromLuminance(luminance) {
  return luminance < 128 ? 'white' : 'black';
}

/**
 * Convert epoch time (seconds) to a local date string.
 * @param {number} epoch - Epoch time in seconds.
 * @returns {string} Formatted local datetime.
 */
function epochToStrDate (epoch) {
  const date = new Date(epoch * 1000);
  const pad = (n) => n.toString().padStart(2, '0');
  return `${date.getFullYear()}-${pad(date.getMonth() + 1)}-${pad(date.getDate())} ` +
         `${pad(date.getHours())}:${pad(date.getMinutes())}:${pad(date.getSeconds())}`;
}

/**
 * Converts a Date object to a formatted string: "YYYY-MM-DD HH:MM".
 *
 * @param {Date} date - The Date object to format.
 * @returns {string} The formatted date string.
 *
 * @example
 * const now = new Date();
 * const formatted = dateToStr(now);
 * console.log(formatted); // "2025-03-29 14:30"
 */
function dateToStr (date) {
  const year = date.getFullYear();
  const month = String(date.getMonth() + 1).padStart(2, '0'); // Months are 0-indexed
  const day = String(date.getDate()).padStart(2, '0');
  const hours = String(date.getHours()).padStart(2, '0');
  const minutes = String(date.getMinutes()).padStart(2, '0');

  return `${year}-${month}-${day} ${hours}:${minutes}`;
}

/**
 * Returns the current local time offset from UTC in seconds.
 *
 * This function calculates the difference between the local time
 * and Coordinated Universal Time (UTC), taking into account
 * daylight saving time if applicable.
 *
 * @returns {number} The offset in seconds. Positive if local time is ahead of UTC,
 *                   negative if behind.
 */
function getLocalUtcOffsetInSeconds() {
  const now = new Date();
  return -now.getTimezoneOffset() * 60;
}

/**
 * Formats a duration given in seconds into a human-readable string.
 * 
 * @param {number} seconds - The duration in seconds.
 * @returns {string} - The formatted duration as "Xj HH:MM:SS".
 */
function formatDuration (seconds) {
   let sign = "";
   if (seconds < 0) {
      sign = "- ";
      seconds = -seconds;
   } 
   let days = Math.floor (seconds / (24 * 3600));
   let hours = Math.floor ((seconds % (24 * 3600)) / 3600);
   let minutes = Math.floor ((seconds % 3600) / 60);
   let secs = Math.round (seconds % 60);
   let withS = days >= 2 ? "s" : "";

   return `${sign}${days} Day${withS} ${String(hours).padStart(2, '0')}:${String(minutes).padStart(2, '0')}:${String(secs).padStart(2, '0')}`;
}

/**
 * Formats a duration given in seconds into a human-readable string.
 * 
 * @param {number} seconds - The duration in seconds.
 * @returns {string} - The formatted duration as "HH:MM:SS".
 */
function formatDurationShort (seconds) {
   let sign = "";
   if (seconds < 0) {
      sign = "- ";
      seconds = -seconds;
   } 
   let hours = Math.floor (seconds / 3600);
   let minutes = Math.floor ((seconds % 3600) / 60);
   let secs = Math.round (seconds % 60);
   return `${sign}${String(hours).padStart(2, '0')}:${String(minutes).padStart(2, '0')}:${String(secs).padStart(2, '0')}`;
}

/**
 * Converts a coordinate in DMS format to decimal degrees.
 * @param {string} dms - The coordinate in DMS format (e.g., "46°24'33"N").
 * @returns {number} The decimal degrees equivalent of the DMS input.
 */
function dmsToDecimal (dms) {
  if (!dms) return 0;
  const parts = dms.match(/(\d+)\u00B0(?:\s*(\d+)')?(?:\s*(\d+(?:\.\d+)?)\")?\s*([NSEW])/);
  if (!parts) return 0;
  let [_, d, m, s, dir] = parts;
  let decimal = parseFloat(d);
  if (m) decimal += parseFloat(m) / 60;
  if (s) decimal += parseFloat(s) / 3600;
  if (dir === 'S' || dir === 'W') decimal *= -1;
  return decimal;
}

/**
 * Converts latitude and longitude to a formatted string in DMS format.
 * @param {number} lat - The latitude in decimal degrees.
 * @param {number} lon - The longitude in decimal degrees.
 * @returns {string} The formatted coordinate string.
 */
function latLonToStr(lat, lon) {
   function toDMS(deg, isLat) {
      const abs = Math.abs(deg);
      const d = Math.floor(abs);
      const m = Math.floor((abs - d) * 60);
      const s = (abs - d - m / 60) * 3600;
      const dir = isLat ? (deg >= 0 ? 'N' : 'S') : (deg >= 0 ? 'E' : 'W');

      // Padding: degrees (3), minutes (2), seconds (5: xx.xx)
      const degStr = String(d).padStart(3, '0');
      const minStr = String(m).padStart(2, '0');
      const secStr = s.toFixed(2).padStart(5, '0');

      return `${degStr}°${minStr}&apos;${secStr}" ${dir}`;
   }

   return `${toDMS(lat, true)} - ${toDMS(lon, false)}`;
}


/**
 * Converts latitude and longitude to a formatted DMS (Degrees, Minutes, Seconds) string.
 * This function converts decimal latitude and longitude coordinates into 
 * a human-readable string in the DMS format, including the appropriate hemisphere indicator (N/S/E/W).
 *
 * @param {number} lat - Latitude in decimal degrees.
 * @param {number} lon - Longitude in decimal degrees.
 * @returns {string} The coordinates formatted as a DMS string (e.g., "48°51'24\"N, 2°21'03\"E").
 */
function toDMSString (lat, lon) {
   function convert (coord, isLat) {
      const absolute = Math.abs(coord);
      const degrees = Math.floor(absolute);
      const minutes = Math.floor((absolute - degrees) * 60);
      const seconds = Math.floor((absolute - degrees - minutes / 60) * 3600);
      const direction = isLat ? (coord >= 0 ? 'N' : 'S') : (coord >= 0 ? 'E' : 'W');

      return `${String(degrees).padStart(2, '0')}°${String(minutes).padStart(2, '0')}'${String(seconds).padStart(2, '0')}"${direction}`.trim();
   }
   return `${convert(lat, true)}, ${convert(lon, false)}`.trim ();
}

/**
 * Finds the nearest port to the given latitude and longitude.
 *
 * Uses the `orthoDist()` function to compute the orthodromic (great-circle) distance.
 * If the coordinates are outside the defined bounding box, returns LE_POULIGUEN as default.
 *
 * @param {number} lat - Latitude in decimal degrees.
 * @param {number} lon - Longitude in decimal degrees.
 * @returns {{idPort: number, idName: string}} An object containing the ID and name of the nearest port.
 */
function findNearestPort(lat, lon) {
   const LAT_MIN = 42, LAT_MAX = 52, LON_MIN = -6, LON_MAX = 4;

   if (lat < LAT_MIN || lat > LAT_MAX || lon < LON_MIN || lon > LON_MAX) {
      return { idPort: 115, namePort: "LE_POULIGUEN" };
   }

   let nearest = ports[0];
   let minDistance = orthoDist([lat, lon], [nearest.lat, nearest.lon]);

   for (let i = 1; i < ports.length; i++) {
      const p = ports[i];
      const dist = orthoDist([lat, lon], [p.lat, p.lon]);
      if (dist < minDistance) {
         minDistance = dist;
         nearest = p;
      }
   }
   return { idPort: nearest.id, namePort: nearest.name };
}


