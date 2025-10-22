const MS_TO_KN = (3600.0/1852.0);   // conversion meter/second to knots

const DMS_DISPLAY = {BASIC: 0, DD: 1, DM: 2, DMS: 3};

/*const sailLegend = {
  NA:     { bg: "black",  luminance: 0 },
  C0:     { bg: "green",  luminance: 85 },
  HG:     { bg: "purple", luminance: 48 },
  Jib:    { bg: "gray",   luminance: 128 },
  LG:     { bg: "blue",   luminance: 29 },
  LJ:     { bg: "yellow", luminance: 210 },
  Spi:    { bg: "orange", luminance: 170 },
  SS:     { bg: "red",    luminance: 76 }
};*/

const sailLegend = {
  NA:     { bg: "black",  luminance: 0 },
  Jib:    { bg: "gray",   luminance: 128 },
  Spi:    { bg: "orange", luminance: 170 },
  SS:     { bg: "red",    luminance: 76 },
  Staysail: { bg: "red",    luminance: 76 },
  LJ:     { bg: "yellow", luminance: 210 },
  LightJib: { bg: "yellow",   luminance: 210 },
  C0:     { bg: "green",  luminance: 85 },
  Code0:    { bg: "green",  luminance: 85 },
  HG:     { bg: "purple", luminance: 48 },
  HeavyGnk: { bg: "purple", luminance: 48 },
  LG:     { bg: "blue",   luminance: 29 },
  LightGnk: { bg: "blue",   luminance: 29 },
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
 * @returns {string} - The formatted duration as "X Days HH:MM:SS".
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
 * Converts a coordinate in DMS format to decimal degree".
 * @param {string} dms - The coordinate in DMS format (e.g., "46°24'33"N").
 * @returns {number} The decimal degrees equivalent of the DMS input.
 */
function dmsToDecimal (dms) {
   if (!dms) return 0;
   let decimal, m, s;
   if ((dms.includes("°")) && (dms.includes("'")) && (dms.includes('"'))) {// X° Y' Z" 
      decimal = parseFloat(dms);
      m = parseFloat(dms.split('°')[1]);
      s = parseFloat(dms.split("'")[1]);
      if (m) decimal += parseFloat(m) / 60;
      if (s) decimal += parseFloat(s) / 3600;
      if (dms.includes('S') || dms.includes('W')) decimal *= -1;
      return decimal;
   }
   if ((dms.includes("°")) && (dms.includes("'"))) {// X° Y'
      decimal = parseFloat (dms);
      m = parseFloat(dms.split('°')[1]);
      if (m) decimal += m / 60;
      if (dms.includes('S') || dms.includes('W')) decimal *= -1;
      return decimal;
   }
   decimal = parseFloat(dms.replaceAll ('°', '')); // X° or X
   if (dms.includes('S') || dms.includes('W')) decimal *= -1;
   return decimal; // case if value is a decimal number
}

/**
 * Converts latitude and longitude to a formatted string.
 * DMS (Degrees, Minutes, Seconds) string ex: 45°28'30 S" or
 * DM (Degrees, Minutes) ex: 45°28.5 S' string or
 * DD (Decimal Degrees) ex: 45.48° S string or
 * BASIC ex -45.48 (with sign if negative)
 * @param {number} lat - The latitude in decimal degrees.
 * @param {number} lon - The longitude in decimal degrees.
 * @returns {string} The coordinates formatted as a DMS string (e.g., "48°51'24\"N, 2°21'03\"E").
 */
function latLonToStr(lat, lon, type=DMS_DISPLAY.DMS) {
   function toDMS (val, isLat, type) {
      const dir = isLat ? (val >= 0 ? 'N' : 'S') : (val >= 0 ? 'E' : 'W');
      const absVal = Math.abs (val);
      const d = Math.floor(absVal);
      let m, degStr, minStr;
      switch (type) {
      case DMS_DISPLAY.BASIC: return val.toFixed (4);
      case DMS_DISPLAY.DD: return `${absVal.toFixed(4)}° ${dir}`;
      case DMS_DISPLAY.DM:
         m = (absVal - d) * 60;
         degStr = String(d).padStart(3, '0');
         minStr = m.toFixed (2).padStart(2, '0');
         return `${degStr}°${minStr}'${dir}`;
      case DMS_DISPLAY.DMS:
         m = Math.floor((absVal - d) * 60);
         s = (absVal - d - m / 60) * 3600;
         degStr = String(d).padStart(3, '0');
         minStr = String(m).padStart(2, '0');
         secStr = s.toFixed(0).padStart(2, '0');
         return `${degStr}°${minStr}'${secStr}"${dir}`;
      default: return 0;
      }
   }
   return `${toDMS(lat, true, type)} - ${toDMS(lon, false, type)}`;
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
   let minDistance =Infinity;
   let nearest = null;
   for (let p of ports) {
      const dist = orthoDist([lat, lon], [p.lat, p.lon]);
      if (dist < minDistance) {
         minDistance = dist;
         nearest = p;
      }
   }
   return { idPort: nearest.id, namePort: nearest.name };
}

