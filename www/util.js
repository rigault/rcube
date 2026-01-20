const EARTH_RADIUS_NM = 3440.065; // Earth average radius in Nautical Miles
const DEG_TO_RAD = Math.PI / 180;
const RAD_TO_DEG = 180 / Math.PI;
const MS_TO_KN = (3600.0/1852.0); // conversion meter/second to knots
const DMS_DISPLAY = {BASIC: 0, DD: 1, DM: 2, DMS: 3};

const sailLegend = {
  NA:        { bg: "black",  luminance: 0 },
  JIB:       { bg: "gray",   luminance: 128 },
  SPI:       { bg: "orange", luminance: 170 },
  SS:        { bg: "red",    luminance: 76 },
  STAYSAIL:  { bg: "red",    luminance: 76 },
  LJ:        { bg: "yellow", luminance: 210 },
  LIGHTJIB:  { bg: "yellow", luminance: 210 },
  LIGHT_JIB: { bg: "yellow", luminance: 210 },
  C0:        { bg: "green",  luminance: 85 },
  CODE0:     { bg: "green",  luminance: 85 },
  CODE_0:    { bg: "green",  luminance: 85 },
  HG:        { bg: "purple", luminance: 48 },
  HEAVYGNK:  { bg: "purple", luminance: 48 },
  HEAVY_GNK: { bg: "purple", luminance: 48 },
  LG:        { bg: "blue",   luminance: 29 },
  LIGHTGNK:  { bg: "blue",   luminance: 29 },
  LIGHT_GNK: { bg: "blue",   luminance: 29 },
};

function isMobile() {
  return window.matchMedia("(pointer: coarse)").matches ||
      /iPhone|iPad|iPod|Android/i.test(navigator.userAgent);
}

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
 * return sage HTML string
 * @param {string} the string to consider 
 * @returns {string} the string after replacements
 */
function esc(s) {
   return String(s ?? '').replace(/[&<>"']/g, m => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[m]));
}

/**
 * Extracts a short display name from a polar path:
 * - keeps only what follows the first "/"
 * - removes file extension
 * - limits length to maxLen characters
 *
 * @param {string} polarPath
 * @param {number} maxLen
 * @returns {string}
 */
function formatPolarName(polarPath, maxLen = 15) {
  if (!polarPath) return "";

  // Keep what follows the first "/"
  const i = polarPath.indexOf("/");
  let name = (i >= 0) ? polarPath.slice(i + 1) : polarPath;

  // Remove extension (last ".xxx")
  name = name.replace(/\.[^.]+$/, "");

  // Limit length
  if (name.length > maxLen) {
    name = name.slice(0, maxLen - 1) + "…";
  }

  return name;
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
   if ((dms.includes("°")) && (dms.includes("'")) && ((dms.includes('"') || dms.includes('″')))) {// X° Y' Z" 
      decimal = parseFloat(dms);
      m = parseFloat(dms.split('°')[1]);
      s = parseFloat(dms.split("'")[1]);
      if (m) decimal += m / 60;
      if (s) decimal += s / 3600;
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
      let m, s, degStr, minStr, secStr;
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
         return `${degStr}°${minStr}'${secStr}″${dir}`;
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
function findNearestPort(lat, lon, ports4tide) {
   const LAT_MIN = 42, LAT_MAX = 52, LON_MIN = -6, LON_MAX = 4;

   if (lat < LAT_MIN || lat > LAT_MAX || lon < LON_MIN || lon > LON_MAX) {
      return { idPort: 115, namePort: "LE_POULIGUEN" };
   }
   let minDistance =Infinity;
   let nearest = null;
   for (let p of ports4tide) {
      const dist = orthoDist(lat, lon, p.lat, p.lon);
      if (dist < minDistance) {
         minDistance = dist;
         nearest = p;
      }
   }
   return { idPort: nearest.id, namePort: nearest.name };
}

/**
 * Choose a file
 * @param {string} dir
 * @param {string} currentFile
 * @param {boolean} byName sort by name if true, by date otherwise
 * @returns {Promise<string|null>} le nom du fichier ou null si annulé
 */
async function chooseFile(dir, currentFile, byName, boxTitle) {
  const formData = `type=${REQ.DIR}&dir=${encodeURIComponent(dir)}${byName ? "&sortByName=true" : ""}`;
  console.log("Request:", formData);

  try {
    const response = await fetch(apiUrl, {
      method: "POST",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body: formData
    });

    const data = await response.json();
    if (!Array.isArray(data) || data.length === 0) {
      await Swal.fire("Erreur", "No file found", "error");
      return null;
    }

    console.log("Response:", JSON.stringify(data));

    // data = [ [name, size, date], ... ] → on extrait les noms
    const names = data.map(row => row[0]);

    // SweetAlert2 'select' attend un objet { value: label }
    const inputOptions = Object.fromEntries(names.map(n => [n, n]));

    const { isConfirmed, value } = await Swal.fire({
      title: boxTitle ?? "File Select",
      input: "select",
      inputOptions,
      inputValue: names.includes(currentFile) ? currentFile : names[0],
      position: "top",
      showCancelButton: true,
      confirmButtonText: "Confirm",
      cancelButtonText: "Cancel",
      customClass: { popup: "swal-wide" }
    });

    if (!isConfirmed) return null;

    console.log("Selected file:", value);
    return value; // <- directement le nom du fichier
  } catch (error) {
    console.error("Error in file request:", error);
    await Swal.fire("Erreur", "Impossible to get File List", "error");
    return null;
  }
}

/**
 * Download a text file
 * @param {string} filename
 * @param {string} content
 */
function downloadTextFile(filename, content) {
   const blob = new Blob([content], { type: "text/plain;charset=utf-8" });
   const url = URL.createObjectURL(blob);

   const a = document.createElement("a");
   a.href = url;
   a.download = filename;
   document.body.appendChild(a);
   a.click();
   document.body.removeChild(a);

   URL.revokeObjectURL(url); // cleaning
}

/**
 * Computes the initial orthodromic (great-circle) bearing from one point to another.
 *
 * This function calculates the initial heading (bearing) in degrees required 
 * to travel from the first point (lat0, lon0) to the second (lat1, lon1) along a great-circle route.
 * 
 * @param {number} lat0 - The latitude starting point
 * @param {number} lon0 - The longitude starting point
 * @param {number} lat1 - The latitude destination point
 * @param {number} lon1 - The longitude destination point
 * @returns {number} The initial bearing in degrees, ranging from 0 to 360°.
 */
function orthoCap (lat0, lon0, lat1, lon1) {
   lat0 *= DEG_TO_RAD;
   lon0 *= DEG_TO_RAD;
   lat1 *= DEG_TO_RAD;
   lon1 *= DEG_TO_RAD;
   
   const dLon = lon1 - lon0;
   const y = Math.sin(dLon) * Math.cos(lat1);
   const x = Math.cos(lat0) * Math.sin(lat1) - Math.sin(lat0) * Math.cos(lat1) * Math.cos(dLon);
   const heading = Math.atan2(y, x);

   return ((RAD_TO_DEG * heading) + 360) % 360;
}

/**
 * Calculates the orthodromic (great-circle) distance between two points.
 *
 * @param {number} lat0 - The latitude starting point
 * @param {number} lon0 - The longitude starting point
 * @param {number} lat1 - The latitude destination point
 * @param {number} lon1 - The longitude destination point
 * @returns {number} Distance in nautical miles.
 */
function orthoDist (lat0, lon0, lat1, lon1) {
   lat0 *= DEG_TO_RAD;
   lon0 *= DEG_TO_RAD;
   lat1 *= DEG_TO_RAD;
   lon1 *= DEG_TO_RAD;
   
   const deltaSigma = Math.acos(
      Math.sin(lat0) * Math.sin(lat1) +
      Math.cos(lat0) * Math.cos(lat1) * Math.cos(lon1 - lon0)
   );

   return EARTH_RADIUS_NM * deltaSigma;
}

/**
 * Computes the initial loxodromic (rhumb line) bearing from one point to another.
 *
 * @param {number} lat0 - The latitude starting point
 * @param {number} lon0 - The longitude starting point
 * @param {number} lat1 - The latitude destination point
 * @param {number} lon1 - The longitude destination point
 * @returns {number} Initial bearing in degrees from 0 to 360.
 */
function loxoCap (lat0, lon0, lat1, lon1) {
   lat0 *= DEG_TO_RAD;
   lon0 *= DEG_TO_RAD;
   lat1 *= DEG_TO_RAD;
   lon1 *= DEG_TO_RAD;
   let dLon = lon1 - lon0;

   const dPhi = Math.log(Math.tan(Math.PI / 4 + lat1 / 2) / Math.tan(Math.PI / 4 + lat0 / 2));

   // Correct dLon for antimeridian cross
   if (Math.abs(dLon) > Math.PI) {
      dLon = dLon > 0 ? -(2 * Math.PI - dLon) : (2 * Math.PI + dLon);
   }

   let bearing = Math.atan2(dLon, dPhi) * RAD_TO_DEG;
   return (bearing + 360) % 360;
}

/**
 * Calculates the loxodromic (rhumb line) distance between two points.
 *
 * @param {number} lat0 - The latitude starting point
 * @param {number} lon0 - The longitude starting point
 * @param {number} lat1 - The latitude destination point
 * @param {number} lon1 - The longitude destination point
 * @returns {number} Distance in nautical miles.
 */
function loxoDist (lat0, lon0, lat1, lon1) {
   lat0 *= DEG_TO_RAD;
   lon0 *= DEG_TO_RAD;
   lat1 *= DEG_TO_RAD;
   lon1 *= DEG_TO_RAD;
   let dLon = Math.abs(lon1 - lon0);
   const dLat = lat1 - lat0;

   const dPhi = Math.log(Math.tan(Math.PI / 4 + lat1 / 2) / Math.tan(Math.PI / 4 + lat0 / 2));

   const q = Math.abs(dPhi) > 1e-12 ? dLat / dPhi : Math.cos(lat0);

   // Correct dLon for antimeridian cross
   if (dLon > Math.PI) {
      dLon = 2 * Math.PI - dLon;
   }

   const distance = Math.sqrt(dLat * dLat + q * q * dLon * dLon);
   return EARTH_RADIUS_NM * distance;
}

/**
 * Computes the great-circle path (orthodromic route) between two geographical points.
 *
 * @param {number} lat0
 * @param {number} lon0
 * @param {number} lat1
 * @param {number} lon1
 * @param {number} [n=100]
 * @returns {Array<[number, number]>} array of [lat, lon]
 */
function getGreatCirclePath(lat0, lon0, lat1, lon1, n = 100) {
  const epsilon = 0.001;
  const path = [];

  lat0 *= DEG_TO_RAD; lon0 *= DEG_TO_RAD;
  lat1 *= DEG_TO_RAD; lon1 *= DEG_TO_RAD;

  // central angle distance
  const cosd = Math.sin(lat0) * Math.sin(lat1) +
               Math.cos(lat0) * Math.cos(lat1) * Math.cos(lon1 - lon0);
  const d = Math.acos(Math.max(-1, Math.min(1, cosd)));

  // almost identical points
  if ((Math.abs(lat1 - lat0) < epsilon) && (Math.abs(lon1 - lon0) < epsilon)) {
    return [[lat0 * RAD_TO_DEG, lon0 * RAD_TO_DEG], [lat1 * RAD_TO_DEG, lon1 * RAD_TO_DEG]];
  }

  for (let i = 0; i <= n; i++) {
    const f = i / n;

    const A = Math.sin((1 - f) * d) / Math.sin(d);
    const B = Math.sin(f * d) / Math.sin(d);

    const x = A * Math.cos(lat0) * Math.cos(lon0) + B * Math.cos(lat1) * Math.cos(lon1);
    const y = A * Math.cos(lat0) * Math.sin(lon0) + B * Math.cos(lat1) * Math.sin(lon1);
    const z = A * Math.sin(lat0) + B * Math.sin(lat1);

    const lat = Math.atan2(z, Math.sqrt(x * x + y * y));
    const lon = Math.atan2(y, x);

    path.push([lat * RAD_TO_DEG, lon * RAD_TO_DEG]);
  }

  return path;
}

/**
 * Condense list of time stamps in concise way 
 * @param {number[]} timeStamps
 * @returns {string}
 */
function condenseTimeStamps (timeStamps) {
   let result = [];
   if (!timeStamps || timeStamps.length === 0) return "[]";
   if (timeStamps.length < 5) {
      for (let i = 0; i < timeStamps.length; i++) {
         result.push (timeStamps [i]);
      }
      return "[" + result.join(", ") + "]";
   }

   let start = timeStamps[0];
   let prev = start;
   let timeStep = null;
   let diff = null;
   let afterStart = timeStamps [1];
   for (let i = 1; i < timeStamps.length; i++) {
      diff = timeStamps[i] - prev;

      if (timeStep === null) {
         timeStep = diff; // Initialize first time step
      }

      if (diff !== timeStep) {
         // New sequence found
         if (prev !== start) {
            result.push(start + (prev !== start + timeStep ? ", " + afterStart + ".." + prev : ""));
            afterStart = start + diff;
         } else {
            result.push(start);
         }
         start = timeStamps[i];
         timeStep = diff;
      }

      prev = timeStamps[i];
   }
   afterStart = start + diff;

   // Add last segment
   if (prev !== start) result.push(start + ", " + afterStart + ".." + prev);
   else result.push(start);

   return "[" + result.join(", ") + "]";
}

