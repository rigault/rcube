/**
 * Fetch polygons from the API and draw them on a Leaflet map.
 *
 * Expects the server response to be an array of polygons,
 * where each polygon is an array of [lat, lon] points.
 *
 * @param {L.Map} map - Leaflet map instance.
 * @param {L.LayerGroup|L.FeatureGroup} [targetLayer] - Optional layer group to hold polygons. If not provided, a new LayerGroup is created.
 * @returns {Promise<L.LayerGroup>} Resolves to the layer group containing the polygons.
 */
async function drawPolygons(map, targetLayer) {
   if (!map) throw new Error("Leaflet map instance is required.");
   // Ensure we have a layer to draw into.
   const layer = targetLayer instanceof L.LayerGroup ? targetLayer : L.layerGroup().addTo(map);
   // Clear previous polygons (idempotent redraw).
   layer.clearLayers();

   let response;
   try {
      response = await fetch(apiUrl, {
         method: "POST",
         headers: {
            "Content-Type": "application/x-www-form-urlencoded; charset=UTF-8"
         },
         body: `type=${REQ.FORBID_ZONE}`,
         cache: "no-store",
         credentials: "omit",
      });
   } catch (networkErr) {
      console.error("Network error while fetching polygons:", networkErr);
      throw new Error("Network error while fetching polygons.");
   }

   if (!response.ok) {
      const msg = `Server error: ${response.status} ${response.statusText}`;
      console.error(msg);
      throw new Error(msg);
   }

   let polygons;
   try {
      polygons = await response.json();
   } catch (parseErr) {
      console.error("Invalid JSON from server:", parseErr);
      throw new Error("Invalid JSON from server.");
   }

   // Basic validation and drawing
   if (!Array.isArray(polygons)) {
      throw new Error("Unexpected response shape: expected an array of polygons.");
   }

   polygons.forEach((poly, idx) => {
      if (!Array.isArray(poly) || poly.length < 3) {
         console.warn(`Skipping polygon #${idx}: not enough points or invalid shape.`);
         return;
      }
      // Leaflet accepts [lat, lng] as given;
      // Attach a className to style via external CSS.
      const polygon = L.polygon(poly, {
         className: "zonePolygon"
      });
      polygon.addTo(layer);
   });
   return layer;
}

