/* jshint esversion: 6 */
let animation = false;
let windyPlay = true;
let clipBoard = false; // to set request in clipBoard
let gribLimits = {
   bottomLat: 0,
   rightLon: 0,
   leftLon: 0,
   topLat: 0,
   name : "",      // wind grib name
   currentName: "" // current grib name
}; 
let moduloIsoc = 1;
let gribRectangle = null;
let polarName = "Ultim.csv";
let polWaveName = "polwave.csv";
let isoDescMarkers = [];

let competitors = [
  { name: "pistache", lat: 46, lon: -3, color: 0, marker: {}},
  { name: "jojo",     lat: 47, lon: -4, color: 1, marker: {}},
  { name: "titi",     lat: 48, lon: -5, color: 2, marker: {}}
];

/*let competitors = [
  { name: "noname", lat: 46, lon: -3, color: 0, marker: {}, route: [] },
];*/

window.routeParam = {
   iBoat: 1,               // 0 reserved for all boats
   isoStep: 1800,          // 30 mn
   startTime: new Date (),
   nTry: 0,                // 0 equivalent to 1 try
   timeInterval: 0,        // 0 means not used
   epochStart: 0,          // Unix time in seconds
   polar: `pol/${polarName}`,
   forbid: true,
   isoc: true,
   isoDesc: false,
   xWind: 1,
   maxWind: 100,
   penalty0: 180,
   penalty1: 180,
   penalty2: 0,
   motorSpeed: 2,
   threshold: 2,
   dayEfficiency: 1.0,
   nightEfficiency: 1.0,
   cogStep: 2,
   cogRange: 90,
   jFactor: 50,
   kFactor: 40,
   nSectors: 720,
   model: "GFS",
   constWindTws: 0, constWindTwd:0, constWave:0, constCurrentS:0, constCurrentD: 0
};

let rCubeInfo = "© René Rigault";
let index = 0;
const options = {
   key: rCubeKey,
   lat: competitors [0].lat,                                
   lon: competitors [0].lon,
   zoom: 4,
   latlon: true,
   timeControl: true
};
let bounds;
let marker;
let map;
let store;
let destination = null;
let orthoRoute = null;
let orthoRouteGroup;
// tables init
let POIs = [];
let myWayPoints = [];   // contains waypoint up to destination not origin
let route = null;       // global variable storing route
let isochroneLayerGroup;
let bestTimeResult = [];
let compResult = [];
//window.oldRoutes = [];

// Define color mapping based on index
const colorMap = ["red", "green", "blue", "orange", "black"];

// equivalent server side: enum {REQ_TEST, REQ_ROUTING, ...}; // type of request
const REQ = {TEST: 0, ROUTING: 1, COORD: 2, RACE: 3, POLAR: 4, 
             GRIB: 5, DIR: 6, PAR_RAW: 7, PAR_JSON: 8,
             INIT: 9, FEEDBACK:10, DUMP_FILE:11, NEAREST_PORT:12}; 

const MARKER = encodeURIComponent(`<?xml version="1.0" encoding="UTF-8" standalone="no"?>
        <!DOCTYPE svg PUBLIC "-//W3C//DTD SVG 1.1//EN" "http://www.w3.org/Graphics/SVG/1.1/DTD/svg11.dtd">
        <svg width="100%" height="100%" viewBox="0 0 14 14" version="1.1" xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" xml:space="preserve" style="fill-rule:evenodd;clip-rule:evenodd;stroke-linejoin:round;stroke-miterlimit:1.41421;">
        <path d="M4.784,13.635c0,0 -0.106,-2.924 0.006,-4.379c0.115,-1.502 0.318,-3.151 0.686,-4.632c0.163,-0.654 0.45,-1.623 0.755,-2.44c0.202,-0.54 0.407,-1.021 0.554,-1.352c0.038,-0.085 0.122,-0.139 0.215,-0.139c0.092,0 0.176,0.054 0.214,0.139c0.151,0.342 0.361,0.835 0.555,1.352c0.305,0.817 0.592,1.786 0.755,2.44c0.368,1.481 0.571,3.13 0.686,4.632c0.112,1.455 0.006,4.379 0.006,4.379l-4.432,0Z" style="fill:rgb(0,46,252);"/><path d="M5.481,12.731c0,0 -0.073,-3.048 0.003,-4.22c0.06,-0.909 0.886,-3.522 1.293,-4.764c0.03,-0.098 0.121,-0.165 0.223,-0.165c0.103,0 0.193,0.067 0.224,0.164c0.406,1.243 1.232,3.856 1.292,4.765c0.076,1.172 0.003,4.22 0.003,4.22l-3.038,0Z" style="fill:rgb(255,255,255);fill-opacity:0.846008;"/>
    </svg>`);
const MARKER_ICON_URL = `data:image/svg+xml;utf8,${MARKER}`;

const BoatIcon = L.icon ({
   iconUrl: MARKER_ICON_URL,
   iconSize: [24, 24],
   iconAnchor: [12, 12],
   popupAnchor: [0, 0],
});

/**
 *Give the date associated with index
 * @param {number} index - Inde
 * @returns {Date} The date
 * should consider the case with waypoints that influence time
 */
function getDateFromIndex (index, boatName) {
   if (!route || !route [boatName] || !route [boatName].track) {
      console.error ("Invalid data route !");
      return;
   }
   let [wp, lat, lon, time, dist, sog, twd, tws, hdg, twa, g, w, stamina, sail, motor] = route [boatName].track [index];
   //let theTime = new Date(routeParam.startTime.getTime() + index * routeParam.isoStep * 1000);
   let theTime = new Date(routeParam.startTime.getTime() + time * 1000);
   return theTime;
}

/**
 * Give visibility to tool bars only when route is active.
 */
function updateToolsVisibility() {
   const toolsDiv = document.getElementById('tools');
   if (route && Object.keys(route).length > 0) {
      toolsDiv.style.display = 'block';
      const isocContEl = document.getElementById('isocCont');
      if (routeParam.isoc && compResult.length === 0) isocContEl.style.display = 'inline'; 
      else isocContEl.style.display = 'none';
   } else {
      toolsDiv.style.display = 'none';
   }
}

/*
 * to display or not display windy timeline 
 */ 
function setTimelineVisible(visible) {
   const windyEl = document.getElementById('windy');
   if (!windyEl) return;
    windyEl.classList.toggle('no-timeline', !visible);
}

/**
 * Saves the current state of competitors and polarName to localStorage.
 * Only simple serializable fields are stored (name, lat, lon, color).
 */
function saveAppState() {
   const cleanCompetitors = competitors.map(c => ({
      name: c.name,
      lat: c.lat,
      lon: c.lon,
      color: c.color
   }));

   localStorage.setItem("competitors", JSON.stringify(cleanCompetitors));
   localStorage.setItem("polarName", polarName);
   localStorage.setItem("POIs", JSON.stringify(getSerializablePOIs()));
   localStorage.setItem("myWayPoints", JSON.stringify(myWayPoints));
   localStorage.setItem("windyPlay", windyPlay);
}

/**
 * Show point waypoints and destination
 * Give the right direction to boats toward first waypoin if requested
 * @param {Array} wayPoints - List of waypoints as [latitude, longitude].
 */
function showWayPoint (wayPoints, headingRequested = true) {
   if (wayPoints.length > 0) {
      for (let boat of competitors) {
         drawOrtho (boat, wayPoints);
         if (headingRequested) {
            let heading = orthoCap ([boat.lat, boat.lon], wayPoints [0]);
            // alert (`name: ${boat.name}, heading: ${heading}`);
            boat.marker.setLatLng ([boat.lat, boat.lon]);                   // Move the mark
            boat.marker._icon.setAttribute ('data-heading', heading); 
            updateIconStyle (boat.marker);
         }
      }
      const lat = myWayPoints [myWayPoints.length - 1][0];
      const lon = myWayPoints [myWayPoints.length - 1][1];
      showDestination (lat, lon);
   }
}

/**
 * Deletes all POIs from the map, memory and localStorage.
 * @param {Array} POIs - The array of POIs to delete (will be emptied).
 */
function deleteAllPOIs () {
   // Remove each marker from the map
   for (const poi of POIs) {
      if (poi._marker) {
          map.removeLayer(poi._marker);
       }
   }
   POIs.length = 0;    // Clear the array
   saveAppState();     // Update storage
   map.closePopup();   // Optional: close any open popup
}

/** 
 * Create string that will be shown in POI popup
 * @param {Object} poi - The poi.
 */
function button4POI (poi) {
   return `
      <span style="margin-right: 8px;"><strong>${poi.name}</strong></span>
      <button onclick="deletePOI('${poi.id}')" class="poi-delete-button" title="Delete this POI">🗑️</button>
   `;
}

/**
 * Show points of interest
 * @param {Array} POIs - List of POIs.
 */
function showPOI (POIs) {
   for (const poi of POIs) {
      const marker = L.marker([poi.lat, poi.lon]).addTo(map);
      poi._marker = marker;
      marker.bindPopup (button4POI (poi));
   }
}

/**
 * Deletes a POI by its index in the array.
 * Removes it from the map, from the array, and from localStorage.
 * @param {number} index - Index of the POI to delete
 */
function deletePOI(id) {
   const index = POIs.findIndex(poi => poi.id === id);
   if (index !== -1) {
      const poi = POIs[index];

      if (poi._marker) {
         map.removeLayer(poi._marker);
      }

      POIs.splice(index, 1);
      saveAppState();
      map.closePopup();
   }
}

/**
 * Extract subset of POI information (no marker)
 * useful for state management 
 */
function getSerializablePOIs () {
    return POIs.map(poi => ({
        id: poi.id,
        name: poi.name,
        lat: poi.lat,
        lon: poi.lon
        // no marker
    }));
}

/**
 * Adds a Point of Interest (POI) at the specified latitude and longitude.
 * This function prompts the user to enter a name for the POI using SweetAlert,
 * then adds it to the `POIs` list and places a marker on the map.
 *
 * @param {number} lat - Latitude of the POI.
 * @param {number} lon - Longitude of the POI.
 */
function addPOI(lat, lon) {
   Swal.fire({
      title: "POI",
      input: "text",
      inputPlaceholder: "Name",
      showCancelButton: true,
      confirmButtonText: "Add",
      cancelButtonText: "Cancel",
      inputValidator: (value) => {
         if (!value) {
            return "Name cannot be empty";
         }
      }
   }).then((result) => {
      if (result.isConfirmed) {
         const name = result.value;
         const id = crypto.randomUUID(); // ID unique (compatible navigateurs récents)

         const poi = { id, lat, lon, name };
         POIs.push(poi);
         saveAppState();

         const marker = L.marker([lat, lon])
            .addTo(map)
            .bindPopup (button4POI (poi))
            .openPopup();

         poi._marker = marker;
      }
   });

   closeContextMenu();
}

/**
 Loads competitors and polarName from localStorage, if available.
 Recreates dynamic Leaflet objects like markers and polylines. 
 */
function loadAppState() {
   const saved = localStorage.getItem ("competitors");
   const savedPolar = localStorage.getItem ("polarName");
   const savedPOIs = localStorage.getItem ("POIs");
   const savedWaypoints = localStorage.getItem ("myWayPoints");
   const savedWindyPlay = localStorage.getItem ("windyPlay");
   if (saved) {
      const parsed = JSON.parse(saved);
      competitors = parsed.map(c => ({
         name: c.name,
         lat: c.lat,
         lon: c.lon,
         color: c.color,
         marker: {},
         routePolyline: []
     }));
   }
   if (savedPolar) polarName = savedPolar;
   if (savedPOIs) POIs = JSON.parse (savedPOIs);
   if (savedWaypoints) myWayPoints = JSON.parse(savedWaypoints);
   if (savedWindyPlay) windyPlay = savedWindyPlay;
}

/**
 * Deletes a competitor from the global competitors array.
 * Removes their marker from the map and updates localStorage.
 * @param {Object} competitor - The competitor object to delete.
 */
function deleteCompetitor(competitor) {
   if (competitors.length <= 1) { // full table cannot be empty
      Swal.fire({
         icon: "error",
         title: "No way",
         text: "Forbidden to delete last boat.",
         confirmButtonText: "OK"
      });
      return;
   }
   const index = competitors.indexOf(competitor);
   if (index !== -1) {
      clearRoutes (); 
      // Supprimer le marker de la carte s’il existe
      if (competitor.marker && map.hasLayer(competitor.marker)) {
         map.removeLayer(competitor.marker);
      }

      // Supprimer du tableau
      competitors.splice(index, 1);
      orthoRouteGroup.clearLayers(); // clear waypoints on map
      for (let boat of competitors)  // redraw waypoints for remaining boats
         drawOrtho (boat, myWayPoints);

      // Mettre à jour le stockage
      saveAppState();
      updateBoatSelect();
   }
}

/**
 * Delete a competitor by name (used from HTML).
 * @param {string} name - Name of the competitor to delete.
 */
function deleteCompetitorByName(name) {
    const competitor = competitors.find(c => c.name === name);
    if (competitor) {
        deleteCompetitor(competitor);
    }
}

/** 
 * Create string that will be shown in boat (compertitor) popup
 * @param {Object} poi - The poi.
 */
function popup4Comp (competitor) {
   return `
      <span style="margin-right: 8px;"><strong>${competitor.name}</strong></span>
      <button onclick="deleteCompetitorByName('${competitor.name}')" class="poi-delete-button" title="Delete this Comp">🗑️</button>
   `;
}

/**
 * Draw grib limits
 * @param {Object} Grib limis
 * @returns {Array}. Bounds of grib
 */
function drawGribLimits (gribLimits) {
   if (!gribLimits || gribLimits.name === "") return;
   
   if (gribRectangle) {
      map.removeLayer(gribRectangle); // remove former rectangle if exists
   }

   let bounds = [
      [gribLimits.bottomLat, gribLimits.leftLon],
      [gribLimits.topLat, gribLimits.rightLon]
   ];

   gribRectangle = L.rectangle (bounds, {
      color: 'black',
      weight: 2,
      fillOpacity: 0 // only border
   }).addTo(map);

   map.invalidateSize(); // Force Leaflet to recompute drawings
   return bounds;
}

/**
 * Simplifies a list of coordinates by removing points that are too close to each other.
 * This uses a simple radial distance simplification method.
 *
 * @param {Array.<Array.<number>>} coords - Array of coordinates [latitude, longitude].
 * @param {number} tolerance - The distance tolerance for simplification (in degrees).
 * @returns {Array.<Array.<number>>} A simplified array of coordinates.
 */
function simplify (coords, tolerance) {
   if (coords.length <= 2) return coords;

   const sqTolerance = tolerance * tolerance;

   function getSqDist (p1, p2) {
      const dx = p1[1] - p2[1];
      const dy = p1[0] - p2[0];
      return dx * dx + dy * dy;
   }

   function simplifyRadialDist(points, sqTolerance) {
      let prevPoint = points[0];
      const newPoints = [prevPoint];
      let point;

      for (let i = 1, len = points.length; i < len; i++) {
         point = points[i];
         if (getSqDist(point, prevPoint) > sqTolerance) {
            newPoints.push(point);
            prevPoint = point;
         }
      }
      if (prevPoint !== point) newPoints.push(point);
      return newPoints;
   }
   return simplifyRadialDist(coords, sqTolerance);
}

/**
 * Draws a polyline on the map using Leaflet.
 *
 * @param {Array<Array<number>>} coords - An array of coordinates where each element is a tuple [latitude, longitude].
 * @param {string} color - The color of the polyline.
 */
function drawPolyline(coords, color) {
   if (!coords || coords.length < 2) return;

   // 1. Oriiginal polyline 
   L.polyline(coords, {
      color: 'red',
      weight: 4,
      opacity: 0.8,
      dashArray: '4, 4'
   });// .addTo(isochroneLayerGroup);

   // 2. Simplification
   const simplifiedCoords = simplify(coords, 0.010);

   // 3. Draw simplified
   L.polyline(simplifiedCoords, {
      color: color,
      weight: 2,
      opacity: 0.8
   }).addTo(isochroneLayerGroup);
}

/** 
 * Show route in color associated to the competitor
 * @param {Object} The route
 * @param {string} Name of boat (idem competitor)
*/
function showRoute (route, name) {
   console.log (`Boat: ${name}`);

   const track = route [name].track;
   if (track.length === 0) return;
   // Convert format for Windy (Leaflet)
   const latlngs = track.map(coords => coords.slice(1, 3)); // lat, lon at pos 1 and 2
   //console.log (JSON.stringify (latlngs, null, 2));
   let iComp = competitors.findIndex (c => c.name === name); // index of current boat
   if (iComp < 0 ) iComp = 0;
   /*Si existe, l'ancienne  route change de couleur
   if (competitors[iComp].routePolyline) {
      window.oldRoutes = window.oldRoutes || [];
      window.oldRoutes.push(competitors[iComp].routePolyline); // Attention, copie par référence

      if (typeof competitors[iComp].routePolyline.setStyle === 'function') {
         competitors[iComp].routePolyline.setStyle({ color: 'pink' });
      } else {
         console.warn('routePolyline does not support setStyle');
      }
   }*/

   const routeColor = colorMap [competitors [iComp].color];
   if (competitors [iComp].routePolyline && typeof competitors [iComp].routePolyline.remove === 'function')
      competitors [iComp].routePolyline.remove ();
   competitors [iComp].routePolyline = L.polyline (latlngs, {color: routeColor}).addTo(map);
   competitors[iComp].marker.setLatLng ([competitors [iComp].lat, competitors [iComp].lon]); // Move the mark
   updateHeading (competitors [iComp], track);
   updateBindPopup (competitors [iComp]);
}

/**
 * Updates the Windy map by displaying the calculated route and optional isochrones.
 *
 * @param {Array<Array<number>>} route - The main sailing route as an array of coordinate pairs [latitude, longitude].
 * @param {Array<Array<number>>}  [isocArray=[]] - An optional array of isochrones, where each isochrone is a list of coordinate pairs.
 */
function updateWindyMap (route) {
   const boatName = Object.keys(route)[0]; // Extract first key from response
   if (!route || !route [boatName] || !route [boatName].track) {
      console.error ("Invalid data route !");
      return;
   }
   let isocArray = [];
   if ("_isoc" in route) isocArray = route ["_isoc"];
   
   isochroneLayerGroup.clearLayers ();

   if ((Array.isArray (isocArray)) && (moduloIsoc !== 0))
      for (let i = 0; i < isocArray.length; i += 1) 
         if ((i % moduloIsoc) === 0) {
            const coords = isocArray[i].map(entry => [entry[0], entry[1]]);
            drawPolyline (coords, "blue");
         }

   let isoDescArray = [];
   isoDescMarkers.forEach(marker => map.removeLayer(marker));
   isoDescMarkers = [];
   if (routeParam.isoDesc) {
      if ("_isodesc" in route) isoDescArray = route["_isodesc"];

      if (Array.isArray(isoDescArray)) {
         for (let i = 0; i < isoDescArray.length; i++) {
               const [nIsoc, wayPoint, size, first, closest, bestVmc, biggestOrthoVmc, focalLat, focalLon] = isoDescArray[i];
               const marker = L.marker([focalLat, focalLon])
                     .addTo(map)
                     .bindPopup(`nIsoc: ${nIsoc}<br>size: ${size}<br>closest: ${closest}<br>bestVmc: ${bestVmc}`);
               isoDescMarkers.push(marker);
         }
      }
   }

   competitors.forEach (refreshMarker);
   for (const [name, data] of Object.entries (route)) {
      if (! name.startsWith("_"))
         showRoute (route, name.trim());
   }

   gribLimits.name = route [boatName].grib;
}

/**
 * Builds the request body for sending data to the server.
 * 
 * @param {string} reqType - The type of request.
 * @param {Array} competitors - List of competitor boats.
 * @param {Array} myWayPoints - List of waypoints as [latitude, longitude].
 * @param {Object} routeParam - Object containing route parameters.
 * @returns {string} The formatted request body.
 */
function buildBodyRequest (reqType, competitors, myWayPoints, routeParam) {
   const waypoints = myWayPoints
      .map(wp => `${wp[0]},${wp[1]}`)
      .join(";");

   // Select the boat(s)
   let boats;
   if (reqType === REQ.RACE) {
      boats = competitors
         .map(c => `${c.name}, ${c.lat}, ${c.lon}`)
         .join(";") + ";";
   } else {
      let c = competitors[routeParam.iBoat - 1];
      console.log("boat: " + `${c.name}, ${c.lat}, ${c.lon}`);
      boats = `${c.name}, ${c.lat}, ${c.lon};`;
   }

   // Define request parameters with safer defaults
   const reqParams = {
      type: reqType,
      boat: boats,
      waypoints: waypoints,
      timeStep: (routeParam.isoStep ?? 1800),
      epochStart: Math.floor(routeParam.startTime.getTime() / 1000),
      polar: `pol/${polarName}`,
      wavePolar: `wavepol/${polWaveName}`,
      forbid: (routeParam.forbid ?? "true"),
      isoc: (routeParam.isoc ?? "false"),
      isodesc: (routeParam.isoDesc ?? "false"),
      withWaves: (routeParam.withWaves ?? "false"),
      withCurrent: (routeParam.withCurrent ?? "false"),
      timeInterval: (routeParam.timeInterval ?? 0),
      xWind: isNaN(routeParam.xWind ?? NaN) ? 1 : routeParam.xWind,
      maxWind: isNaN(routeParam.maxWind ?? NaN) ? 100 : routeParam.maxWind,
      penalty0: isNaN(routeParam.penalty0 ?? NaN) ? 0 : routeParam.penalty0,
      penalty1: isNaN(routeParam.penalty1 ?? NaN) ? 0 : routeParam.penalty1,
      penalty2: isNaN(routeParam.penalty2 ?? NaN) ? 0 : routeParam.penalty2,
      motorSpeed: isNaN(routeParam.motorSpeed ?? NaN) ? 0 : routeParam.motorSpeed,
      threshold: isNaN(routeParam.threshold ?? NaN) ? 0 : routeParam.threshold,
      dayEfficiency: isNaN(routeParam.dayEfficiency ?? NaN) ? 1.0 : routeParam.dayEfficiency,
      nightEfficiency: isNaN(routeParam.nightEfficiency ?? NaN) ? 1.0 : routeParam.nightEfficiency,
      cogStep: isNaN(routeParam.cogStep ?? NaN) ? 5 : routeParam.cogStep,
      cogRange: isNaN(routeParam.cogRange ?? NaN) ? 90 : routeParam.cogRange,
      jFactor: isNaN(routeParam.jFactor ?? NaN) ? 0 : routeParam.jFactor,
      kFactor: isNaN(routeParam.kFactor ?? NaN) ? 0 : routeParam.kFactor,
      nSectors: isNaN(routeParam.nSectors ?? NaN) ? 1 : routeParam.nSectors,
      model: routeParam.model,
      constWindTws: isNaN(routeParam.constWindTws ?? NaN) ? 0 : routeParam.constWindTws,
      constWindTwd: isNaN(routeParam.constWindTwd ?? NaN) ? 0 : routeParam.constWindTwd,
      constWave: isNaN(routeParam.constWave ?? NaN) ? 0 : routeParam.constWave,
      constCurrentS: isNaN(routeParam.constCurrentS ?? NaN) ? 0 : routeParam.constCurrentS,
      constCurrentD: isNaN(routeParam.constCurrentD ?? NaN) ? 0 : routeParam.constCurrentD
   };

   let requestBody = Object.entries(reqParams)
      .map(([key, value]) => `${key}=${value}`)
      .join("&");
   
   if (gribLimits.name && typeof gribLimits.name === "string" && gribLimits.name.trim().length > 1) {
      // requestBody += `&grib=grib/${gribLimits.name}`; // name of grib can be deduced by the sever thanks to model specification
      if (gribLimits.currentName.trim().length > 1)
         requestBody +=`&currentGrib=currentgrib/${gribLimits.currentName}`;
   }
   
   return requestBody;
}

/**
 * Launch HTTP request using fetch, with the request body .
 *
 * @param {String} - requestBody - the request body.
 *
 */
async function handleRequest (requestBody) {
   console.log ("handleRequest: " + requestBody);
   const headers = { "Content-Type": "application/x-www-form-urlencoded" };
   const token = btoa(`${userId}:${password}`);
   const auth = `Basic ${token}`;
   console.log ("token: " + token);
   if (auth && userId !== "") headers.Authorization = auth;     // else stay anonymous level 0
   //alert ("UserId: " + userId + " password: " + password);
   try {
      const response = await fetch (apiUrl, {
         method: "POST",
         headers,
         body: requestBody,
         cache: "no-store"
      });

      const data = await response.json();

      console.log(JSON.stringify(data, null, 2));
      const firstKey = Object.keys(data)[0];

      if (firstKey.startsWith("_")) {
         await Swal.fire({
            title: "Warning",
            text: firstKey + ": " + data[firstKey],
            icon: "warning",
            confirmButtonText: "OK"
         });
         return null;
      }

      return { boatName: firstKey, routeData: data };

   } catch (error) {
      console.error("Fetch error:", error);
      await Swal.fire({
         icon: "error",
         title: "Request Failed",
         text: "The server is not responding or returned an invalid response.",
         confirmButtonText: "OK"
      });
      return null;
   }
}

/**
 * Update display after getting server response.
 *
 * @param {String} - boatName - the name of boat or competitor.
 */
function finalUpdate (boatName) {
   const iComp = competitors.findIndex (c => c.name === boatName); // index of current boat
   //competitors [iComp].route = route; // save route in competitir object
   //alert ("competitor: " + competitors [iComp].name);
   goBegin ();
   updateWindyMap (route);

   gribLimits.bottomLat = route [boatName]?.bottomLat || gribLimits.bottomLat;
   gribLimits.leftLon = route [boatName]?.leftLon || gribLimits.leftLon;
   gribLimits.topLat = route [boatName]?.topLat || gribLimits.topLat;
   gribLimits.rightLon = route [boatName]?.rightLon || gribLimits.rightLon;
   gribLimits.name = route [boatName]?.grib || gribLimits.name;

   drawGribLimits (gribLimits);
   updateStatusBar (route);
}

/**
 * Creates and displays a centered progress bar on the screen.
 *
 * The progress bar is styled to be centered with a gray background and an orange fill.
 * It automatically injects CSS styles the first time it is called.
 *
 * @param {number} maxValue - The maximum value for the progress bar (i.e., the total number of steps).
 * @returns {HTMLProgressElement} The created and appended progress element.
 */
function createProgressBar(maxValue) {
    // Create container
    let container = document.createElement('div');
    container.style.position = 'fixed';
    container.style.top = '50%';
    container.style.left = '50%';
    container.style.transform = 'translate(-50%, -50%)';
    container.style.width = '300px';
    container.style.height = '30px';
    container.style.zIndex = '500';
    container.style.backgroundColor = '#eee';
    container.style.border = '1px solid #ccc';
    container.style.borderRadius = '5px';
    container.style.overflow = 'hidden';
    container.style.fontFamily = 'Arial, sans-serif';
    container.style.textAlign = 'center';
    container.style.lineHeight = '30px'; // center text vertically
    container.style.color = '#333';
    container.style.fontWeight = 'bold';
    container.style.fontSize = '16px';
    container.style.userSelect = 'none'; // avoid text selection

    // Create a progress inner div
    let progressInner = document.createElement('div');
    progressInner.style.height = '100%';
    progressInner.style.width = '0%';
    progressInner.style.backgroundColor = 'orange';
    progressInner.style.borderRadius = '5px 0 0 5px';
    progressInner.style.transition = 'width 0.3s'; // smooth animation

    // Create a percentage text
    let percentage = document.createElement('div');
    percentage.textContent = '0%';
    percentage.style.position = 'absolute';
    percentage.style.top = '50%';
    percentage.style.left = '50%';
    percentage.style.transform = 'translate(-50%, -50%)';
    percentage.style.pointerEvents = 'none'; // clicks go through

    // Add to DOM
    container.appendChild(progressInner);
    container.appendChild(percentage);
    document.body.appendChild(container);

    // Function to update progress
    container.update = function(value) {
        let percent = Math.min(100, Math.round((value / maxValue) * 100));
        progressInner.style.width = percent + '%';
        percentage.textContent = percent + '%';
    };

    return container;
}

/**
 * Constructs and prepares the request body for a routing API call.
 *
 * Request for only one boat, at different time defined by routeParam.
 */
async function requestBestTime () {
   let saveStartTime = routeParam.startTime;
   let result = [];
   let bestDuration = Infinity;
   let progress = createProgressBar(routeParam.nTry);
   let boatName, routeData;

   await new Promise(resolve => setTimeout(resolve, 0));
   compResult.length = 0; // inhibate display of competitor dashboard
   for (let i = 0; i < routeParam.nTry; i++) {
      routeParam.startTime = new Date(saveStartTime.getTime() + i * routeParam.timeInterval * 1000);
      let requestBody = buildBodyRequest (REQ.ROUTING, competitors, myWayPoints, routeParam);

      if (clipBoard && i === 0) {
         navigator.clipboard.writeText(requestBody);
      }

      const response = await handleRequest (requestBody);

      if (response) {
         ({ boatName, routeData } = response);
         if (! routeData [boatName].destinationReached)  // stop when first unreached found
            break;
         let duration = routeData [boatName].duration;
         result.push (duration);
         if (duration < bestDuration) {
            duration = bestDuration;
            route = routeData; // keep best solution in global variable
         }
        // alert('i = ' + i + ' epochStart = ' + routeParam.startTime + ' boatName: ' + boatName + ' duration = ' + routeData.duration);
      } else {
         console.error('Request failed at i =', i);
      }
      progress.update (i + 1);
   }

   console.log ("All durations:", result);
   progress.remove ();
   routeParam.startTime = saveStartTime;
   bestTimeResult = result;
   dispBestTimeHistogram (bestTimeResult, routeParam.startTime, routeParam.timeInterval);
   if (result.length !== 0) {
      finalUpdate (boatName);
   }
}

/**
 * Constructs and prepares the request body for a routing API call.
 *
 * Request for all boats, at startTime.
 */
async function requestAllCompetitors () {
   let saveIBoat = routeParam.iBoat;
   let result = [];
   let progress = createProgressBar (competitors.length);
   await new Promise(resolve => setTimeout(resolve, 0));
   let lastBoatName;
   route = {};
   let OK = false;
   const saveIsoc = routeParam.isoc;
   routeParam.isoc = false; // force no Isochrones if all competitors

   for (let i = 0; i < competitors.length; i++) {
      routeParam.iBoat = i + 1;
      let requestBody = buildBodyRequest (REQ.ROUTING, competitors, myWayPoints, routeParam);

      if (clipBoard && i === 0) {
         navigator.clipboard.writeText(requestBody);
      }

      const response = await handleRequest (requestBody);

      if (response) {
         const { boatName, routeData } = response;
         if (! routeData [boatName].destinationReached)  // stop when first unreached found
            result.push (-1);
         else {
            let duration = routeData [boatName].duration;
            result.push (duration);
            Object.assign (route, routeData);            // add the new route found
            if (! boatName.startsWith("_"))
               showRoute (routeData, boatName.trim());
            lastBoatName = boatName;
            OK = true;
         }
      } else {
         console.error('Request failed at i =', i);
         OK = false;
         break;
      }
      progress.update (i + 1);
   }

   console.log ("All durations:", result);
   routeParam.iBoat = saveIBoat;
   dispAllCompetitors (result);
   progress.remove ();
   compResult = result; // saved in global variable
   routeParam.isoc = saveIsoc;
   if (OK) finalUpdate (lastBoatName);
}

/**
 * Constructs and prepares the request body for a routing API call.
 *
 * Request for anly one boat, ine time
 */
async function requestOne () {
   const spinnerOverlay = document.getElementById ("spinnerOverlay");
   spinnerOverlay.style.display = "flex"; // display spinner
   compResult.length = 0; // inhibate display of competitor dashboard
   let requestBody = buildBodyRequest (REQ.ROUTING, competitors, myWayPoints, routeParam);

   if (clipBoard) navigator.clipboard.writeText (requestBody);
   const response = await handleRequest (requestBody);
   if (response) {
      const { boatName, routeData } = response;
      console.log ("requestOne:" + JSON.stringify(routeData, null, 2));
      route = routeData; // global variable
      showRouteReport (routeData);  
      finalUpdate (boatName);
   } else {
      console.error('Request failed');
   }
   spinnerOverlay.style.display = "none";
}

/**
 * Prepare routing API call.
 *
 * Depending on routeParameter, select between requestBestTime, requestAllCompetitors or requestOne
 */
function request () {
   if (myWayPoints === null || myWayPoints === undefined || myWayPoints.length === 0) {
      Swal.fire({
         icon: "error",
         title: "Invalid Request",
         text: "No waypoints or destination defined.",
         confirmButtonText: "OK"
      });
      return;
   }
   clearRoutes ();

   if (routeParam.nTry > 1 && routeParam.timeInterval !== 0) {
      requestBestTime ();
      return;
   }
   if (routeParam.iBoat === 0) { // iboat == 0 means all competitors
      requestAllCompetitors ();
      return;
   }

   requestOne ();
}

/**
 * Replay the routing request:
 * 1. Displays an empty input box to paste a query manually.
 * 2. Parses the input into `routeParam`, `myWayPoints`, and `competitors`.
 * 3. Triggers the `request()` function.
 */
async function replay () {
   try {
      // Show input dialog (empty by default)
      const { value: queryString } = await Swal.fire({
         title: 'Replay routing request',
         input: 'textarea',
         inputLabel: 'Paste routing query manually',
         inputValue: '',
         inputAttributes: {
            'aria-label': 'Routing query input'
         },
         showCancelButton: true,
         confirmButtonText: 'Replay',
         cancelButtonText: 'Cancel',
         width: '60em'
      });

      if (!queryString) return;

      // Parse query string to key-value pairs
      const params = new URLSearchParams(queryString);
      const get = key => params.get(key);

      // --- Parse waypoints into myWayPoints ---
      /**
       * Parses waypoints string into array of [lat, lon]
       * @param {string} str - Semicolon-separated list of "lat,lon"
       */
      const parseWaypoints = str => str.split(';').map(s => {
         const [lat, lon] = s.split(',').map(Number);
         return [lat, lon];
      });

      // --- Parse competitors string into array of objects ---
      /**
       * Parses boat string into array of competitor objects
       * @param {string} str - Comma-separated: name,lat,lon;name,lat,lon;...
       */
      const parseCompetitors = str => {
         const list = str
         .split(';')
         .map(s => s.trim())              // clean spaces
         .filter(s => s.length > 0)       // skip empty entries
         .map((entry, idx) => {
            const [name, lat, lon] = entry.split(',').map((v, i) => i === 0 ? v.trim() : Number(v));
            return {
               name,
               lat,
               lon,
               color: idx
            };
         });
         return list;
      };

      // Build routeParam (excluding waypoints)
      window.routeParam = {
         iBoat: parseInt(get('type') || 1),
         isoStep: parseInt(get('timeStep') || 1800),
         startTime: new Date(),
         nTry: 0,
         timeInterval: parseInt(get('timeInterval') || 0),
         epochStart: parseInt(get('epochStart') || 0),
         polar: get('polar'),
         wavePolar: get('wavePolar'),
         forbid: get('forbid') === 'true',
         isoc: get('isoc') === 'true',
         isoDesc: get('isodesc') === 'true',
         withWaves: get('withWaves') === 'true',
         withCurrent: get('withCurrent') === 'true',
         xWind: parseFloat(get('xWind') || 1),
         maxWind: parseFloat(get('maxWind') || 100),
         penalty0: parseInt(get('penalty0') || 0),
         penalty1: parseInt(get('penalty1') || 0),
         penalty2: parseInt(get('penalty2') || 0),
         motorSpeed: parseFloat(get('motorSpeed') || 0),
         threshold: parseFloat(get('threshold') || 0),
         dayEfficiency: parseFloat(get('dayEfficiency') || 1),
         nightEfficiency: parseFloat(get('nightEfficiency') || 1),
         cogStep: parseInt(get('cogStep') || 2),
         cogRange: parseInt(get('cogRange') || 90),
         jFactor: parseFloat(get('jFactor') || 80),
         kFactor: parseFloat(get('kFactor') || 60),
         nSectors: parseInt(get('nSectors') || 720),
         model: get(model),
         constWindTws: parseFloat(get('constWindTws') || 0),
         constWindTwd: parseFloat(get('constWindTwd') || 0),
         constWave: parseFloat(get('constWave') || 0),
         constCurrentS: parseFloat(get('constCurrentS') || 0),
         constCurrentD: parseFloat(get('constCurrentD') || 0)
      };

      routeParam.startTime = new Date (routeParam.epochStart * 1000);

      // Build global waypoints array
      clearRoutes ();
      myWayPoints = parseWaypoints(get('waypoints') || '');
      polarName = routeParam.polar.split('/').pop();
      polWaveName =  routeParam.wavePolar.split('/').pop();
      // Build global competitors array
      for (let competitor of competitors) {
         if (competitor.marker)
            competitor.marker.remove ();
      }
      competitors = parseCompetitors(get('boat') || '');
      for (let competitor of competitors) {
         addMarker (competitor);
         setBoat (competitor, competitor.lat, competitor.lon);
      }
      showWayPoint (myWayPoints);

      // Trigger routing request
      requestOne ();
   } catch (error) {
      console.error('Replay failed:', error);
      Swal.fire('Error', 'Unable to parse the input or start the route.', 'error');
   }
}

/**
 * Computes the great-circle path (orthodromic route) between two geographical points.
 *
 * This function calculates a series of intermediate points along the shortest path
 * between two locations on the Earth's surface, using spherical interpolation.
 *
 * @param {number} lat0 - Latitude of the starting point, in degrees.
 * @param {number} lon0 - Longitude of the starting point, in degrees.
 * @param {number} lat1 - Latitude of the destination point, in degrees.
 * @param {number} lon1 - Longitude of the destination point, in degrees.
 * @param {number} [n=100] - Number of intermediate points to compute along the path.
 * @returns {Array<Array<number>>}  The great-circle path as an array of coordinate pairs [latitude, longitude].
 */
function getGreatCirclePath (lat0, lon0, lat1, lon1, n = 100) {
   let path = [];
   let φ1 = (lat0 * Math.PI) / 180;
   let λ1 = (lon0 * Math.PI) / 180;
   let φ2 = (lat1 * Math.PI) / 180;
   let λ2 = (lon1 * Math.PI) / 180;

   for (let i = 0; i <= n; i++) {
      let f = i / n;
      let A = Math.sin((1 - f) * Math.acos(Math.sin(φ1) * Math.sin(φ2) + Math.cos(φ1) * Math.cos(φ2) * Math.cos(λ2 - λ1)));
      let B = Math.sin(f * Math.acos(Math.sin(φ1) * Math.sin(φ2) + Math.cos(φ1) * Math.cos(φ2) * Math.cos(λ2 - λ1)));

      let x = A * Math.cos(φ1) * Math.cos(λ1) + B * Math.cos(φ2) * Math.cos(λ2);
      let y = A * Math.cos(φ1) * Math.sin(λ1) + B * Math.cos(φ2) * Math.sin(λ2);
      let z = A * Math.sin(φ1) + B * Math.sin(φ2);

      let φ = Math.atan2(z, Math.sqrt(x * x + y * y));
      let λ = Math.atan2(y, x);

      path.push([φ * (180 / Math.PI), λ * (180 / Math.PI)]);
   }
   return path;
}

/**
 * Draws both the loxodromic orthodromic (great-circle) routes 
 * for a given competitor and waypoints.
 *
 * This function extracts the competitor's starting position, and prepares the orthodromic route.
 *
 * @param {Object} competitors - competitor is an aovject containing at least name, lat, lon.
 * @param {Array<Array<number>>} myWayPoints - Array of waypoints as [latitude, longitude] pairs.
 */
function drawOrtho (competitor, myWayPoints) {
   if (!competitor) {
      console.error("Invalid data for competitor.");
      return;
   }
   // extract lat and lon of specified competitor
   let firstPoint = [competitor.lat, competitor.lon];

   // build table of points 
   let routePoints = [firstPoint, ...myWayPoints];
   console.log ("routePoints:", routePoints);
   console.log ("routePoints:", routePoints);

   //orthoRouteGroup.clearLayers();
   // Create orthodromie
   routePoints.forEach ((wp, index) => {
      if (index < routePoints.length - 1) {
         let lat0 = wp [0], lon0 = wp [1];
         let lat1 = routePoints [index + 1][0], lon1 = routePoints[index + 1][1];

         let path = getGreatCirclePath (lat0, lon0, lat1, lon1, 100);
         let polyline = L.polyline (path, { color: 'green', weight: 3, dashArray: '5,5' });

         // Ajoute la polyligne au groupe au lieu de l'écraser
         orthoRouteGroup.addLayer (polyline);
      }
   });
}

/**
 * Clears all routes, waypoints, and markers from the map.
 * This function resets the waypoints list, removes all layers and polylines,
 * and clears markers for the destination and computed routes.
 */
function clearRoutes () {
   isochroneLayerGroup.clearLayers();
   // Delete old routes
   //window.oldRoutes.forEach(route => route.remove());
   //window.oldRoutes = [];
   competitors.forEach(comp => {
      if (comp.routePolyline && typeof comp.routePolyline.remove === 'function') {
         comp.routePolyline.remove();
        // comp.routePolyline = null;
      }
   });
   route = null;
   index = 0;
   competitors.forEach (refreshMarker);
   updateStatusBar (route);
}

/**
 * Resets all waypoints and clears related map elements.
 * This function removes all stored waypoints, clears the loxodromic 
 * and orthodromic routes, and removes the destination marker if it exists.
 */
function resetWaypoint () {
   myWayPoints = [];
   orthoRouteGroup.clearLayers();
   if (destination) destination.remove ();
   saveAppState();
}

/**
 * Updates the boat's position to the specified latitude and longitude.
 * This function moves the selected competitor to the given coordinates,
 * updates its map marker, and redraws the orthodromic route.
 * If waypoints exist, it also updates the boat's heading.
 *
 * @param {Object} competitor is an object containing at least name, latitude, longitude.
 * @param {number} lat - New latitude of the boat.
 * @param {number} lon - New longitude of the boat.
 */
function setBoat (competitor, lat, lon) {
   competitor.lat = lat;
   competitor.lon = lon;

   competitor.marker.setLatLng ([lat, lon]);                   // Move the mark

   console.log("Waypoints:", myWayPoints);
   drawOrtho (competitor, myWayPoints);

   if (myWayPoints.length > 0) {
      let heading = orthoCap([lat, lon], myWayPoints[0]);
      competitor.marker._icon.setAttribute ('data-heading', heading);
      updateIconStyle (competitor.marker);
   }
   closeContextMenu();
   orthoRouteGroup.clearLayers();
   for (let boat of competitors)
      drawOrtho (boat, myWayPoints);
   saveAppState ();
}

/**
 * Add destination at the specified latitude and longitude.
 *
 * @param {number} lat - Latitude of the destination.
 * @param {number} lon - Longitude of the destination.
 */
function showDestination (lat, lon) {
   if (destination) destination.remove();
   destination = L.marker([lat, lon], {
      icon: L.divIcon({
         className: 'custom-destination-icon',
         html: '🏁',
         iconSize: [32, 32],
         iconAnchor: [8, 16]
      })
   }).addTo(map);
}

/**
 * Adds a new waypoint at the specified latitude and longitude.
 * This function appends a new waypoint to `myWayPoints`. If it is the first waypoint, 
 * it also updates the boat's heading toward this point.
 *
 * @param {number} lat - Latitude of the new waypoint.
 * @param {number} lon - Longitude of the new waypoint.
 */
function addWaypoint (lat, lon) {
   if (myWayPoints.length === 0) {
      for (let boat of competitors) {
         // position heading of boat at first waypoint
         let heading = orthoCap([boat.lat, boat.lon], [lat, lon]);
         boat.marker.setLatLng ([boat.lat, boat.lon]);                   // Move the mark
         boat.marker._icon.setAttribute('data-heading', heading); 
         updateIconStyle (boat.marker);
      }
   }

   myWayPoints.push ([lat, lon]);
   for (let boat of competitors)
      drawOrtho (boat, myWayPoints);
   showDestination (lat, lon);
   console.log ("Waypoints:", myWayPoints);
   closeContextMenu();
   saveAppState();
}

/**
 * Updates the icon rotation based on the boat's heading.
 * This function checks if a marker exists and retrieves its heading attribute.
 * If the icon does not already have a rotation applied, it modifies the CSS `transform` property 
 * to rotate the marker according to its heading.
 */
function updateIconStyle (marker) {
   if (!marker) return;
   const icon = marker._icon;
   if (!icon) return;
   const heading = marker._icon.getAttribute('data-heading');
   if (icon.style.transform.indexOf ('rotateZ') === -1) {
      icon.style.transform = `${
         icon.style.transform
      } rotateZ(${heading}deg)`;
      icon.style.transformOrigin = 'center';
   }
}

/**
 * Moves the boat along its track by a given step.
 * This function updates the boat's position based on the given track and step count.
 * It adjusts the index within valid bounds, updates the boat's marker position, 
 * and updates the Windy map timestamp accordingly.
 *
 * @param {Array<Array<number>>} firstTrack - The array of waypoints, where each waypoint is [latitude, longitude].
 * @param {number} n - The step count to move forward (positive) or backward (negative).
 */
function move (iComp, firstTrack, index) {
   if (firstTrack.length === 0 || index >= firstTrack.length) {
      return;
   }
   if (index < 0) {
      index = Math.max (firstTrack.length - 1, 0);
   }
   let time = getDateFromIndex (index, competitors [iComp].name );
   let newLatLng = firstTrack[index].slice(1, 3);     // New position
   console.log ("move: ", iComp, "time: " + time);
   competitors [iComp].marker.setLatLng (newLatLng);  // Move the mark

   // Center map on new boat position
   // map.setView(newLatLng, map.getZoom());
   if (windyPlay) store.set ('timestamp', time);                     // update Windy time
   updateHeading (competitors [iComp], firstTrack);
   updateStatusBar (route); 
}

/**
 * Update bind popup
 */
function updateBindPopup (competitor) {
   let [wp, lat, lon, time, dist, sog, twd, tws, hdg, twa, g, w, stamina, sail, motor] = route [competitor.name].track [index];
   hdg = (360 + hdg) % 360;
   const propulse = (motor ? "Motor": "Sail: " + sail) ?? "-";
   let theDate = new Date (routeParam.startTime.getTime() + time * 1000);
   // alert ("updateBindPopup name: " + competitor.name);
   if (sog !== undefined)
      competitor.marker.bindPopup (`${popup4Comp(competitor)}<br>
         ${dateToStr(theDate)}<br>
         ${latLonToStr (lat, lon, DMSType)}<br>
         Twd: ${twd.toFixed(0)}° Tws: ${tws.toFixed(2)} kn<br>
         Hdg: ${hdg.toFixed(0)}° Twa: ${twa.toFixed(0)}°<br>
         Sog: ${sog.toFixed(2)} kn ${propulse}<br>`);
}

/**
 * Updates the boat's heading based on its current and next position.
 * This function calculates the bearing (heading) between the current position and 
 * the next point in the track, and updates the boat's marker rotation.
 *
 * @param {Array<Array<number>>} firstTrack - The array of waypoints, where each waypoint is [latitude, longitude].
 */
function updateHeading (competitor, firstTrack) {
   if (!firstTrack || firstTrack.length === 0) return;
   let newIndex = (index < firstTrack.length - 1) ? index : index - 1;
   if (newIndex <= 0) newIndex = 0;
   let [, , , , , , , , hdg, , , , , , ] = firstTrack [newIndex];
   competitor.marker._icon.setAttribute('data-heading', hdg);
   updateIconStyle (competitor.marker);
}

/**
 * Common part of goBegin, backWard and forWard.
 * This function calls move for all relevant competitors 
 */
function updateAllBoats () {
   if (!route) 
      return;
   const boatNames = Object.keys(route);
   console.log ("boatNames:", boatNames);
   boatNames.forEach((name, i) => {
      let iComp = competitors.findIndex (c => c.name === name); // index of current boat
      if ((iComp >= 0) && (! name.startsWith("_"))) {
         console.log ("boatName: ", name, "iComp = ", iComp);
         move (iComp, route[name].track, index);
         updateBindPopup (competitors [iComp]);
      }
   });
}

/**
 * Resets the boat's position to the beginning of its track.
 * This function sets the index to 0 and moves the boat to the first waypoint,
 * ensuring time and position are updated accordingly.
 */
function goBegin () {
   index = 0;
   updateAllBoats ();
   stopAnim ();
}

/**
 * Moves the boat one step backward along its track.
 * Calls `move()` with a negative step value to shift the boat to the previous waypoint.
 */
function backWard () {
   index -= 1;
   if (index < 0) index = 0;
   updateAllBoats ();
   stopAnim ();
}

/**
 * Moves the boat continuously along its track.
 */
function playAnim() {
   if (animation) return;                    // avoid double animation
   const boatName = Object.keys(route)[0];   // Extract first key from response
   const len = route[boatName].track.length;
   const icon = document.getElementById('playPauseIcon');
   icon.classList.remove('fa-play');
   icon.classList.add('fa-pause');
   animation = setInterval(() => {
      if (index >= len) {
         stopAnim();
         return;
      }
      // if (index >= len || index < 0) index = 0;
      index += 1;
      updateAllBoats ();
   }, 500); // Intervalle de mise à jour en ms
}

/**
 * Stops boatsboat 
 */
function stopAnim() {
    const icon = document.getElementById('playPauseIcon');
    icon.classList.remove('fa-pause');
    icon.classList.add('fa-play');
    clearInterval(animation);
    animation = false;
}

/**
/**
 * Moves the boat one step forward along its track.
 * Calls `move()` with a positive step value to shift the boat to the next waypoint.
 */
function forWard () {
   index += 1;
   const boatName = Object.keys(route)[0]; // Extract first key from response
   const len = route[boatName].track.length;
   if (index > len) index = len - 1;
   updateAllBoats ();
   stopAnim ();
}

/**
 * Moves the boat to tthe end its track.
 * Calls `move()` with final index to mùove the boat the the end of track.
 */
function goEnd () {
   const boatName = Object.keys(route)[0];  // Extract first key from response
   index = route[boatName].track.length - 1; // the end of the main boat track
   console.log ("in goEnd, last:" + index);
   updateAllBoats ();
   stopAnim ();
}

/**
 * Formats a date into a human-readable string with the weekday, date, and local time.
 * Example output: "Saturday, 2025-03-08 22:44 Local Time"
 *
 * @param {Date} date - The date object to format.
 * @returns {string} The formatted date string.
 */
function formatLocalDate (date) {
    // Get individual date components
   const dayName = date.toLocaleString('en-US', { weekday: 'short' });
   const year = date.getFullYear();
   const month = String(date.getMonth() + 1).padStart(2, '0'); // Ensure two-digit format
   const day = String(date.getDate()).padStart(2, '0');
   const hours = String(date.getHours()).padStart(2, '0');
   const minutes = String(date.getMinutes()).padStart(2, '0');

   return `${dayName}, ${year}-${month}-${day} ${hours}:${minutes} Local Time`;
}

/**
 * Updates status bar information and calculates bounds.
 * If no route is provided, it updates general parameters such as the polar name and GRIB file.
 *
 * @param {Object|null} [route=null] - The route object containing navigation data, or `null` to update global info.
 */
function updateStatusBar (route = null) {
   updateToolsVisibility();
   setTimelineVisible(! route);
   let time = " "; // important to keep space
   let polar = "", wavePolar = "", grib = "", currentGrib = "";
   if (route === null) {
      polar = polarName;
      wavePolar = polWaveName;
      grib = gribLimits.name;
      currentGrib = gribLimits.currentName;
   }
   else if (route) {
      const boatName = Object.keys(route)[0]; // Extract first key from response
      polar = route[boatName].polar;
      wavePolar = route[boatName].wavePolar;
      grib = route[boatName].grib;
      currentGrib = route[boatName].currentGrib;
      time = " 📅 " + formatLocalDate (getDateFromIndex (index, boatName));
      const p =  Math.round((index * 100) / (route[boatName].track.length -1));
      document.getElementById("timeLine").value = String(p);
      document.getElementById("timeLineValue").textContent = String(p).padStart(3, '0') + "%";
   }
   document.getElementById("infoRoute").innerHTML = "";
   if (time.length > 0) document.getElementById("infoTime").innerHTML = time; 
   if (polar.length > 0) document.getElementById("infoRoute").innerHTML += "    ⛵ polar: " + polar; 
   if (wavePolar.length > 0) document.getElementById("infoRoute").innerHTML += "    🌊 wavePolar: " + wavePolar;
   if (grib.length > 0) document.getElementById("infoRoute").innerHTML += "    💨 Grib: " + grib;
   if (currentGrib.length > 0) document.getElementById("infoRoute").innerHTML += "    🔄 currentGrib: " + currentGrib;
}

/**
 * Determines the bounding box for a given set of waypoints.
 *
 * This function calculates the minimum and maximum latitude/longitude values 
 * to define the bounding box that encloses the route.
 *
 * @param {Array<Array<number>>} wayPoints - An array of waypoints as [latitude, longitude] pairs.
 */
function findBounds (wayPoints) {
   if (wayPoints.length < 2) return;
   let lat0 = wayPoints [0][0];
   let lat1 = wayPoints [wayPoints.length - 1][0];
   let lon0 = wayPoints [0][1];
   let lon1 = wayPoints [wayPoints.length - 1][1];
   
   bounds = [
      [Math.floor (Math.min (lat0, lat1)), Math.floor (Math.min (lon0, lon1))],  // Inf left
      [Math.ceil (Math.max (lat0, lat1)), Math.ceil (Math.max (lon0, lon1))]     // Sup right
   ];
   console.log ("bounds: " + bounds);
   return bounds;
} 

/**
 * Closes the existing context menu if it is present on the page.
 *
 * This function removes the context menu from the document if it exists,
 * preventing multiple menus from stacking.
 */
function closeContextMenu() {
    let oldMenu = document.getElementById("context-menu");
    if (oldMenu) {
        document.body.removeChild(oldMenu);
    }
}

/**
 * Adds a new competitor with user input for name and selecting color by click.
 */
function newCompetitor (theLat, theLon) {
   let palette = `<div id="colorPalette" style="display: flex; flex-wrap: wrap; gap: 10px; justify-content: center; margin-top: 10px;">`;
   colorMap.forEach((color, index) => {
      palette += `<div class="color-choice" data-index="${index}" style="
                  width: 24px;
                  height: 24px;
                  border-radius: 50%;
                  background-color: ${color};
                  border: 2px solid #333;
                  cursor: pointer;
                " title="Color ${index}">
                </div>`;
   });
   palette += `</div>`;

   Swal.fire({
      title: 'Add New Competitor',
      html: `
         <input id="competitorName" class="swal2-input" placeholder="Name" autofocus>
         ${palette}
       `,
      focusConfirm: false,
      showCancelButton: true,
      didOpen: () => {
         let selected = null;
         const colorChoices = document.querySelectorAll('.color-choice');
         colorChoices.forEach(choice => {
            choice.addEventListener('click', () => {
               colorChoices.forEach(c => c.style.outline = 'none');
               choice.style.outline = '3px solid #f00'; // Rouge pour la sélection
               selected = parseInt(choice.getAttribute('data-index'), 10);
          // Stocke l'index sélectionné dans un attribut temporaire
               Swal.getPopup().setAttribute('data-selected-color', selected);
            });
         });
      },
      preConfirm: () => {
         const name = document.getElementById('competitorName').value.trim();
         const selectedColor = Swal.getPopup().getAttribute('data-selected-color');

         if (!name) {
            Swal.showValidationMessage('Please enter a name.');
            return false;
         }
         if (selectedColor === null) {
            Swal.showValidationMessage('Please select a color.');
            return false;
         }

         return { name, color: parseInt(selectedColor, 10) };
      }
   }).then((result) => {
      if (result.isConfirmed) {
         const { name, color } = result.value;
         let newCompetitor = { name, lat: theLat, lon: theLon, color, marker: {} };
         competitors.push(newCompetitor);
         addMarker(newCompetitor);
         setBoat (newCompetitor, theLat, theLon);
      }
   });
}

/**
 * Displays a custom context menu at the mouse click position.
 *
 * This function retrieves the latitude and longitude from the event,
 * creates a new context menu element, and positions it based on the 
 * mouse's screen coordinates.
 *
 * @param {Object} e - The event object containing the click position and map coordinates.
 * @param {Object} e.latlng - The latitude and longitude of the clicked point.
 * @param {number} e.latlng.lat - The latitude of the clicked location.
 * @param {number} e.latlng.lng - The longitude of the clicked location.
 * @param {Object} e.originalEvent - The original DOM event containing screen coordinates.
 * @param {number} e.originalEvent.clientX - The X coordinate of the click event on the screen.
 * @param {number} e.originalEvent.clientY - The Y coordinate of the click event on the screen.
 */
function showContextMenu(e) {
   closeContextMenu();

   const lat = e.latlng.lat.toFixed(6);
   const lon = e.latlng.lng.toFixed(6);

   let menu = document.createElement("div");
   menu.id = "context-menu";
   menu.style.top = `${e.originalEvent.clientY}px`;
   menu.style.left = `${e.originalEvent.clientX}px`;

   // Boutons pour chaque compétiteur avec cercle coloré
   let buttons = competitors.map((c, index) => {
      const color = colorMap[c.color % colorMap.length];
      return `
         <button class="context-button" onclick="setBoat(competitors[${index}], ${lat}, ${lon})">
             <span class="color-dot" style="background-color: ${color};"></span>
             Set ${c.name}
         </button>
      `;
   }).join("");

   buttons += `<button class="context-button" onclick="newCompetitor(${lat}, ${lon})">Add Boat</button>`;
   buttons += `<hr style="margin: 6px 0;">`;

   buttons += `
      <button class="context-button" onclick="addWaypoint(${lat}, ${lon})">Add Waypoint or Destination</button>
      <button class="context-button" onclick="resetWaypoint()">Reset Waypoints and Destination</button>
      <button class="context-button" onclick="addPOI(${lat}, ${lon})">Add POI</button>
      <button class="context-button" onclick="deleteAllPOIs()">Delete all POIs</button>
   `;

   menu.innerHTML = buttons;
   document.body.appendChild(menu);
   document.addEventListener("click", closeContextMenu, { once: true });
}

/**
 * Updates the boat's position and marker based on the selected timestamp from Windy.
 *
 * This function calculates the corresponding index in the route data based on 
 * the selected timestamp, updates the boat's position, and moves the marker accordingly.
 *
 * @param {number} selectedTimestamp - The selected timestamp (in seconds) from Windy's timeline.
 */
function updateRouteDisplay (selectedTimestamp) {
   //const boatName = Object.keys(route)[0]; // Extract first key from response
   console.log ('Met à jour la route pour la date :', new Date(selectedTimestamp * 1000).toISOString());
   if (! route) return;
   //let firstTrack = route [boatName].track;

   let startTimeSec = Math.floor(routeParam.startTime.getTime() / 1000);
   index = Math.round((selectedTimestamp - startTimeSec) / routeParam.isoStep);
   console.log ("index avant: " + index);
   
   //if (index >= firstTrack.length) index = firstTrack.length - 1;
   //else 
   if (index < 0) index = 0;
   console.log ("index apres: " + index);

   const boatNames = Object.keys(route);
   console.log ("boatNames:", boatNames);
   boatNames.forEach((name, i) => {
      let iComp = competitors.findIndex (c => c.name === name); // index of current boat
      if ((iComp >= 0) && (! name.startsWith("_"))) {
         let track = route [name].track;
         // console.log ("boatName: ", name, "iComp = ", iComp, "track = " + track);
         if (track.length !== 0 && index < track.length) {
            let newLatLng = track [index].slice (1, 3); // New position
            competitors [iComp].marker.setLatLng (newLatLng);                   // Move the mark
            if (index !==  track.length -1) updateHeading (competitors [iComp], track);
            updateStatusBar (route);
            updateBindPopup (competitors [iComp]);
         }
      }
   });
}

/**
 * Add a marker to competitor
 * @param {Object} competitor with at least name, lat, lon
 * @param {number} index of competitor.
 */
function addMarker (competitor, iComp) {
   let marker = L.marker([competitor.lat, competitor.lon], {
      icon: BoatIcon,
   }).addTo(map);
   marker.bindPopup (popup4Comp (competitor));
   // if (iComp === 0) marker.openPopup(); Only open for first competitor

   competitor.marker = marker;
}

/**
 * Recreate a marker for competitor
 * @param {Object} competitor with at least name, lat, lon
 * @param {number} index of competitor.
 */
function refreshMarker (competitor, iComp) {
   if (competitor.marker)
      competitor.marker.remove ();
   let marker = L.marker([competitor.lat, competitor.lon], {
      icon: BoatIcon,
   }).addTo(map);
   marker.bindPopup (popup4Comp (competitor));
   competitor.marker = marker;
   if (myWayPoints.length > 0) {
      let heading = orthoCap([competitor.lat, competitor.lon], myWayPoints [0]);
      competitor.marker._icon.setAttribute('data-heading', heading); 
      updateIconStyle (competitor.marker);
   }
}

function additionalInit () {
   updateBoatSelect ();
   competitors.forEach (addMarker); // show initial position of boats
   isochroneLayerGroup = L.layerGroup().addTo(map);
   orthoRouteGroup = L.layerGroup().addTo(map);
   
   map.on ("contextmenu", showContextMenu);	

   let isContextMenuOpen = false; // Avoid multiple display

   document.addEventListener("touchstart", function (event) {
      if (isContextMenuOpen) return; // Do not open several contect menus
         let touch = event.touches [0];

      // check if user touch <header>, #tool ou <footer>
      let targetElement = event.target.closest("header, #tool, footer");
      if (targetElement) return; // Ignore one of these elem

      let timeout = setTimeout (() => {
         let latlng = map.containerPointToLatLng([touch.clientX, touch.clientY]);
         isContextMenuOpen = true; // Avoid multiple display
         let fakeEvent = {
            latlng: latlng,
            originalEvent: {
               clientX: touch.clientX,
               clientY: touch.clientY
            }
         };

         showContextMenu (fakeEvent);

         // Authorize menu again after close
         document.addEventListener("click", () => {
            isContextMenuOpen = false;
         }, { once: true });

      }, 500); // 500 ms => long touch

      document.addEventListener("touchmove", function () {
         clearTimeout(timeout); // Annuler si l'utilisateur bouge
      }, { passive: true });

   }, { passive: true });

   map.on ('mousemove', function (event) {
        let lat = event.latlng.lat; //
        let lon = event.latlng.lng; 
        document.getElementById ('coords').textContent = latLonToStr (lat, lon, DMSType);
    });
   // Handle some events. We need to update the rotation of icons ideally each time
   // leaflet re-renders. them.

   map.on("zoomend", function () {
      competitors.forEach(function (competitor) {
         updateIconStyle(competitor.marker);
      });
      drawGribLimits(gribLimits);
   });
   map.on("zoom", function () {
      competitors.forEach(function (competitor) {
         updateIconStyle(competitor.marker);
      });
      drawGribLimits (gribLimits);
   });
   map.on ("viewreset", function () {
      competitors.forEach(function (competitor) {
         updateIconStyle(competitor.marker);
      });
      drawGribLimits (gribLimits);
   });
   getServerInit ();
   updateStatusBar ();
   showWayPoint (myWayPoints, windyPlay);
   showPOI (POIs);
   
   if (gpsActivated && gpsTimer > 0) {
      fetchGpsPosition();
      setInterval (fetchGpsPosition, gpsTimer * 1000);
   }
   if (aisActivated && aisTimer > 0) {
      fetchAisPosition();
      setInterval (fetchAisPosition, aisTimer * 1000);
   }
}

// Initializes a Leaflet map with OpenStreetMap base and OpenSeaMap seamarks overlay,
function initMap(containerId) {
  // --- Map setup ---
  map = L.map(containerId, { zoomControl: true /* preferCanvas: true */ });

  // Base layer: OpenStreetMap
  const osm = L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {
    maxZoom: 19,
    attribution: '&copy; OpenStreetMap contributors'
  }).addTo(map);

  // OpenSeaMap seamarks overlay (on top of the base map)
  const seamark = L.tileLayer('https://tiles.openseamap.org/seamark/{z}/{x}/{y}.png', {
    maxZoom: 18,
    opacity: 1.0,
    attribution: 'Seamarks &copy; OpenSeaMap contributors'
  }).addTo(map);

  // Layers control to toggle overlay/route
  L.control.layers(
    { 'OpenStreetMap': osm },
    { 'OpenSeaMap Seamarks': seamark},
    { collapsed: false }
  ).addTo(map);

   // Scale bar (optional)
   L.control.scale({
      position: 'topleft', 
      imperial: false     
   }).addTo(map);

   // Tip: If you prefer to fit to the route instead of the bbox, use:
   // map.fitBounds(routeLine.getBounds());
   // updateRouteDisplay (0);
}

