/**
 * Perform additional initialization once the base map is ready.
 *
 * This function:
 * - Updates the boat selector UI and adds initial markers for all competitors.
 * - Creates layer groups for isochrones, orthodromic routes and last points.
 * - Sets up various Leaflet event handlers (zoom, move, viewreset, dblclick, mousemove).
 * - Installs a long-press handler on touch devices to open the context menu.
 * - Updates the status bar and coordinates display.
 * - Draws polygons, waypoints, POIs, marks and meters.
 * - Starts periodic GPS and AIS polling if enabled.
 *
 * It assumes that the global `map` object is already initialized (Leaflet map or windy.map).
 *
 * @returns {void}
 */
function additionalInit() {
   updateBoatSelect();
   competitors.forEach(addMarker); // show initial position of boats
   isochroneLayerGroup = L.layerGroup().addTo(map);
   orthoRouteGroup = L.layerGroup().addTo(map);

   map.doubleClickZoom.disable();
   map.on("contextmenu", showContextMenu);

   let isContextMenuOpen = false; // Avoid multiple display

   document.addEventListener("touchstart", function (event) {
      if (isContextMenuOpen) return; // Do not open several context menus
      let touch = event.touches[0];

      // check if user touch <header>, #tool or <footer>
      let targetElement = event.target.closest("header, #tool, footer");
      if (targetElement) return; // Ignore one of these elem

      let timeout = setTimeout(() => {
         let latlng = map.containerPointToLatLng([touch.clientX, touch.clientY]);
         isContextMenuOpen = true; // Avoid multiple display
         let fakeEvent = {
            latlng: latlng,
            originalEvent: {
               clientX: touch.clientX,
               clientY: touch.clientY
            }
         };

         showContextMenu(fakeEvent);

         // Authorize menu again after close
         document.addEventListener("click", () => {
            isContextMenuOpen = false;
         }, { once: true });

      }, 500); // 500 ms => long touch

      document.addEventListener("touchmove", function () {
         clearTimeout(timeout); // Cancel if user moves
      }, { passive: true });

   }, { passive: true });

   map.on('mousemove', function (event) {
      let lat = event.latlng.lat;
      let lon = event.latlng.lng;
      document.getElementById('coords').textContent = latLonToStr(lat, lon, DMSType);
   });

   // Handle some events. We need to update the rotation of icons ideally each time
   // Leaflet re-renders them.
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
      drawGribLimits(gribLimits);
   });
   map.on("viewreset", function () {
      competitors.forEach(function (competitor) {
         updateIconStyle(competitor.marker);
      });
      drawGribLimits(gribLimits);
   });

   getServerInit();
   updateStatusBar();
   showWayPoint(myWayPoints, !mapMode); // !mapMode true if WINDY
   showPOI(POIs);

   const polygonsLayer = L.layerGroup().addTo(map);
   marks = getMarks(map);

   map.on('dblclick', function (e) { // double click for info on lat/lon
      const { lat, lng } = e.latlng;
      coordStatus(lat, lng);
   });

   drawPolygons(map, polygonsLayer)
      .catch(err => console.error(err));

   if (gpsActivated && gpsTimer > 0) {
      fetchGpsPosition();
      setInterval(fetchGpsPosition, gpsTimer * 1000);
   }
   if (aisActivated && aisTimer > 0) {
      fetchAisPosition();
      setInterval(fetchAisPosition, aisTimer * 1000);
   }

   lastPointLayer = L.layerGroup().addTo(map);

   meters(map);
   twaRoutesGroup.addTo(map);
}

/**
 * Load French maritime ports from a GeoJSON file and display them
 * as circle markers on top of the existing Leaflet map.
 *
 * @param {string} [geoFile="geo/Port_Maritime_FRA.geojson"]
 */
function addPorts(map, geoFile = GEO_FILE) {
   fetch(geoFile)
      .then((response) => {
         if (!response.ok) {
            throw new Error("HTTP error " + response.status);
         }
         return response.json();
      })
      .then((data) => {
         // Remove previous ports layer if it already exists
         if (portsLayer) {
            map.removeLayer(portsLayer);
            if (layersControl) {
               layersControl.removeLayer(portsLayer);
            }
         }

         // --- Filter features for Manche + Atlantic (France) ---
         const filteredFeatures = (data.features || []).filter((f) => {
            if (!f.geometry || f.geometry.type !== "Point") return false;
            const coords = f.geometry.coordinates;
            if (!Array.isArray(coords) || coords.length < 2) return false;
            const lon = coords[0];
            const lat = coords[1];
            return inZone (lat, lon);
         });

         console.log(
            "Ports total:", (data.features || []).length,
            "→ Manche/Atlantique:", filteredFeatures.length
         );

         const filteredGeoJson = {
            type: "FeatureCollection",
            name: data.name || "Ports_Manche_Atlantique",
            crs: data.crs,
            features: filteredFeatures
         };

         portsLayer = L.geoJSON(filteredGeoJson, {
            pointToLayer: (feature, latlng) => {
               return L.circleMarker(latlng, {
                  radius: 4,
                  weight: 1,
                  pane: "markerPane",
                  interactive: true,
                  bubblingMouseEvents: true
               });
            },
            onEachFeature: (feature, layer) => {
               const props = feature.properties || {};

               const name =
                  props.NomPort ||
                  props.NomZonePortuaire ||
                  "Unknown port";

               const commune = props.LbCommune || "";
               const activite =
                  props.MnActivitePortuaire_1 ||
                  props.MnActivitePortuaire_2 ||
                  props.MnActivitePortuaire_3 ||
                  "";

               let popupHtml = `<strong>${name}</strong>`;
               if (commune) popupHtml += `<br>${commune}`;
               if (activite) popupHtml += `<br>${activite}`;

               layer.bindPopup(popupHtml);
            }
         });

         // portsLayer.addTo(map); // not visible at start

         if (layersControl) {
            layersControl.addOverlay(portsLayer, "Ports Manche/Atlantique");
         }

         console.log("Filtered ports layer loaded.");
      })
      .catch((err) => {
         console.error("Failed to load ports GeoJSON:", err);
      });
}

/**
 * Initialize a Leaflet map using OpenStreetMap as base layer and
 * OpenSeaMap seamarks as overlay.
 *
 * The created Leaflet map is stored in the global `map` variable and
 * a canvas pane for wind rendering (`windPane` + `windCanvas`) is added
 * on top of the map.
 *
 * @param {string} containerId - DOM element id that will host the Leaflet map.
 * @returns {void}
 */
function initMapOSM(containerId) {
   // --- Map setup ---
   map = L.map(containerId, {
      zoomControl: true /* preferCanvas: true */
   });

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
      { 'OpenSeaMap Seamarks': seamark },
      { collapsed: false }
   ).addTo(map);

   // Scale bar (optional)
   L.control.scale({
      position: 'topleft',
      imperial: false
   }).addTo(map);

   const windPane = map.createPane('windPane');
   windPane.style.zIndex = 350;
   windPane.style.pointerEvents = 'none';

   windCanvas = document.createElement('canvas');
   windCanvas.id = 'wind-layer';
   windCanvas.style.position = 'absolute';
   windCanvas.style.pointerEvents = 'none';

   windPane.appendChild(windCanvas);
   map.on('move zoom resize', drawWind); // or 'moveend', 'zoomend', 'resize'

   // Tip: If you prefer to fit to the route instead of the bbox, use:
   // map.fitBounds(routeLine.getBounds());
   // updateRouteDisplay(0);
}

/**
 * Initialize a Leaflet map using a local GeoJSON file as a land "base layer"
 * instead of remote OpenStreetMap / OpenSeaMap tiles.
 *
 * The created Leaflet map is stored in the global `map` variable and
 * a canvas pane for wind rendering (`windPane` + `windCanvas`) is added
 * on top of the map.
 *
 * The GeoJSON file is loaded from `geo/land_polygons.geojson` and used
 * to draw land polygons; the view is then fitted to the GeoJSON bounds.
 *
 * @param {string} containerId - DOM element id that will host the Leaflet map.
 * @returns {void}
 */
function initMapGeoJson(containerId) {
   const geoFile = "geo/land_polygons.geojson";

   // --- Map setup ---
   map = L.map(containerId, {
      zoomControl: true
   });

   map.setView([0, 0], 2);

   const landLayer = L.geoJSON(null, {
      style: function () {
         return {
            color: "#555555",
            weight: 0.5,
            fillColor: "#dddddd",
            fillOpacity: 1.0
         };
      }
   }).addTo(map);

   const baseLayers = {
      "Land polygons": landLayer
   };

   const overlayLayers = {
      // On remplira plus tard avec "Ports"
   };

   // 🔹 stocker le layersControl dans une globale
   layersControl = L.control.layers(baseLayers, overlayLayers, { collapsed: false }).addTo(map);

   L.control.scale({
      position: "topleft",
      imperial: false
   }).addTo(map);

   // --- Wind canvas pane ---
   const windPane = map.createPane("windPane");
   windPane.style.zIndex = 350;
   windPane.style.pointerEvents = "none";

   windCanvas = document.createElement("canvas");
   windCanvas.id = "wind-layer";
   windCanvas.style.position = "absolute";
   windCanvas.style.pointerEvents = "none";

   windPane.appendChild(windCanvas);
   map.on("move zoom resize", drawWind);

   // --- Load local GeoJSON and add it to the land layer ---
   fetch(geoFile)
      .then(function (response) {
         if (!response.ok) throw new Error(`GeoJSON load failed:" ${response.status}`);
         return response.json();
      })
      .then(function (data) {
         landLayer.addData(data);

         try {
            const bounds = landLayer.getBounds();
            if (bounds.isValid()) {
               map.fitBounds(bounds);
            }
         } catch (e) {
            console.warn("Could not fit bounds from GeoJSON:", e);
         }

         // 🔹 Une fois la carte calée sur la terre, on ajoute les ports
         initPorts (map, ports);
         addPorts(map);
      })
      .catch(function (err) {
         console.error("Error loading GeoJSON '" + geoFile + "':", err);
      });
}

/**
 * Dynamically load the Windy libBoot script and initialize the Windy API.
 *
 * Once the script is loaded, this function:
 * - Calls `windyInit(options, ...)`,
 * - Stores the Windy API object in the global `windy`,
 * - Retrieves the Leaflet map and store (`windy.map`, `windy.store`) into globals,
 * - Normalizes the timestamp and subscribes to timestamp changes,
 * - Calls `additionalInit()` to perform generic map setup (markers, layers, events...).
 *
 * If the script cannot be loaded (offline or network issue), a SweetAlert error is shown.
 *
 * @returns {void}
 */
function loadWindyAndInit() {
   const script = document.createElement('script');
   script.src = 'https://api.windy.com/assets/map-forecast/libBoot.js';
   script.onload = () => {
      // windyInit is now defined by libBoot.js
      windyInit(options, windyAPI => {
         windy = windyAPI;
         map = windy.map;
         store = windy.store;

         let initialTimestamp = store.get('timestamp');
         if (initialTimestamp > 1e10) {
            initialTimestamp = Math.floor(initialTimestamp / 1000);
         }

         store.on('timestamp', (newTimestamp) => {
            if (newTimestamp > 1e10) {
               newTimestamp = Math.floor(newTimestamp / 1000);
            }
            // updateRouteDisplay(newTimestamp);
         });

         additionalInit();
      });
   };
   script.onerror = () => {
      console.error('Failed to load Windy libBoot.js');
      Swal.fire('Error', 'Cannot load Windy API (offline?)', 'error');
   };
   document.head.appendChild(script);
}

/**
 * Initialize the map depending on the chosen mode.
 *
 * Modes are defined in the global `MAP_MODE` enum:
 * - MAP_MODE.WINDY: use Windy API (online), map is provided by Windy.
 * - MAP_MODE.OSM:   use Leaflet with OpenStreetMap and OpenSeaMap layers.
 * - MAP_MODE.LOCAL: use Leaflet with a local GeoJSON land polygons layer.
 *
 * For OSM and LOCAL, this function creates the Leaflet map and then calls
 * `additionalInit()` to setup generic layers and event handlers.
 * For WINDY, it delegates to `loadWindyAndInit()`.
 *
 * @returns {void}
 */
function initMapAccordingToMode() {
   switch (mapMode) {
   case MAP_MODE.WINDY:
      loadWindyAndInit();
      break;
   case MAP_MODE.OSM:
      initMapOSM('windy');
      additionalInit();
      break;
   case MAP_MODE.LOCAL:
      initMapGeoJson('windy');
      additionalInit();
      break;
   default:;
   }
}

/**
 * Ask the user which map mode to use (Windy / OSM / Local GeoJSON)
 * using a SweetAlert2 select dialog.
 *
 * The parameter `formerVal` corresponds to the previously selected mode:
 *   0 → MAP_MODE.WINDY
 *   1 → MAP_MODE.OSM
 *   2 → MAP_MODE.LOCAL
 *
 * If `formerVal` is not valid, the default selection will be 0 (Windy).
 *
 * @async
 * @param {number|string} formerVal - Previous selection value (0, 1, or 2), number or string.
 * @returns {Promise<number>} A promise resolving to the selected map mode.
 */
async function chooseMapMode(formerVal) {
   // Normalize into string to compare
   const formerStr = String(formerVal);
   const allowed = ['0', '1', '2'];
   const defaultValStr = allowed.includes(formerStr) ? formerStr : '0';

   const { value: mode } = await Swal.fire({
      title: 'Select map mode',
      input: 'select',
      inputOptions: {
         '0': '🌬️  Windy (online)',
         '1': '🗺️  OpenStreetMap (online)',
         '2': '📁 Local GeoJSON (offline)'
      },
      inputPlaceholder: 'Choose a mode',
      inputValue: defaultValStr,   // ← string
      confirmButtonText: 'OK',
      allowOutsideClick: false,
      allowEscapeKey: false
   });

   // mode is string '0' | '1' | '2' or undefined if weird
   const finalStr = mode !== undefined ? mode : defaultValStr;
   return parseInt(finalStr);
}
