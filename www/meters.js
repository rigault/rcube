/**
 * Initialize the orthodromic measuring tool on a Leaflet map.
 * - Press 'M' to toggle measure mode on/off.
 * - Click to add waypoints.
 * - Draws great-circle (orthodromic) path between waypoints.
 * - Shows cumulative distance (km + NM) and last leg initial bearing.
 *
 * @param {L.Map} map - The Leaflet map instance.
 */
function meters(map) {
   let measureActive = false;
   let measurePoints = [];
   let measureLine = null;

   function distMeters(a, b) {
      return a.distanceTo(b); // Leaflet great-circle distance
   }

   /**
    * Compute the initial bearing (last leg heading) for the last added segment.
    * Uses user-provided orthoCap(lat0, lon0, lat1, lon1).
    *
    * @param {L.LatLng[]} points - Array of clicked points.
    * @returns {number|null} Bearing in degrees (0..360), or null if not available.
    */
   function getLastBearing(points) {
      if (points.length < 2) return null;

      const p0 = points[points.length - 2];
      const p1 = points[points.length - 1];

      // orthoCap() must exist in the outer scope.
      return orthoCap(p0.lat, p0.lng, p1.lat, p1.lng);
   }

   /**
    * Build the full visible polyline by chaining great-circle segments
    * for every leg in `measurePoints`.
    * Each leg is resampled into many points for better visual accuracy.
    *
    * @param {L.LatLng[]} points - User clicked waypoints.
    * @returns {L.LatLng[]} Array of LatLngs forming the drawn path.
    */
   function buildGreatCirclePath(points) {
      if (points.length === 0) return [];
      if (points.length === 1) return [ points[0] ];

      let bigPath = [];

      for (let i = 1; i < points.length; i++) {
         // 64 samples per leg: smooth great-circle segment.
         const rawArc = getGreatCirclePath(points[i - 1].lat,  points[i - 1].lng, points[i].lat, points[i].lng, 64);
         const arc = rawArc.map (x => L.latLng (x[0], x[1]));

         if (i === 1) {
            // First segment: take everything.
            bigPath = bigPath.concat(arc);
         } else {
            // Next segments: skip first point to avoid duplicates at joins.
            bigPath = bigPath.concat(arc.slice(1));
         }
      }
      return bigPath;
   }

   /**
    * Compute total great-circle distance for all legs defined in `measurePoints`.
    *
    * @param {L.LatLng[]} points - User clicked waypoints.
    * @returns {number} Total distance in meters.
    */
   function totalDistanceMeters(points) {
      let total = 0;
      for (let i = 1; i < points.length; i++) {
         total += distMeters(points[i - 1], points[i]);
      }
      return total;
   }

   /**
    * Refresh visual + popup:
    *  - redraw the great-circle polyline
    *  - compute total distance
    *  - compute last leg bearing
    *  - display popup at the last clicked point
    *
    * @param {L.LatLng} lastClickLatLng - The last clicked position.
    */
   function updateMeasureDisplay(lastClickLatLng) {
      // Build full orthodromic path
      const gcPath = buildGreatCirclePath(measurePoints);

      // Create or update the polyline
      if (measureLine) {
         measureLine.setLatLngs(gcPath);
      } else {
         measureLine = L.polyline(gcPath, { color: "yellow" }).addTo(map);
      }

      // Total traveled distance
      const totalNMVal = totalDistanceMeters(measurePoints) / 1852.0;

      // Last leg bearing
      const lastBrg = getLastBearing(measurePoints);

      // Popup HTML
      let html = "<b>Total distance</b> : " + totalNMVal.toFixed(2) + " NM"; 

      if (lastBrg !== null && !Number.isNaN(lastBrg)) {
         html += "<br><b>Last bearing</b> :" + Math.round(lastBrg) + "°";
      }

      html += "<br><small>Press M to stop / reset</small>";

      // Show popup at last clicked position
      L.popup()
         .setLatLng(lastClickLatLng)
         .setContent(html)
         .openOn(map);
   }

   /**
    * Keyboard handler to toggle measure mode.
    * - Press 'M' to start or stop measuring.
    * - When stopping, everything is cleared.
    */
   function onKeyDown(e) {
      if (e.key === "m" || e.key === "M") {
         measureActive = !measureActive;

         if (!measureActive) {
            // Leaving measure mode: reset all state and clear map.
            measurePoints = [];

            if (measureLine) {
               map.removeLayer(measureLine);
               measureLine = null;
            }

            map.closePopup();
         }

         console.log("Measure mode:", measureActive ? "ON" : "OFF");
      }
   }

   /**
    * Map click handler.
    * When in measure mode, each click adds a waypoint and refreshes the display.
    *
    * @param {Object} e - Leaflet mouse event.
    * @param {L.LatLng} e.latlng - Clicked location.
    */
   function onMapClick(e) {
      if (!measureActive) return;

      // Store clicked point
      measurePoints.push(e.latlng);

      // Redraw polyline and popup
      updateMeasureDisplay(e.latlng);
   }

   document.addEventListener("keydown", onKeyDown);
   map.on("click", onMapClick);
}

