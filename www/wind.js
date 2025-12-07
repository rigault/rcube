/** return iTInf and iTSup in order t : gribLimits.timeStamps[iTInf] <= t <= gribLimits.timeStamps[iTSup] */
function findTimeAround(t) {
  const ts = gribLimits.timeStamps;
  const n  = gribLimits.nTimeStamp;

  if (!t || t < ts[0]) return { iTInf: 0, iTSup: 0 };

  for (let k = 0; k < n; k++) {
    if (t === ts[k]) return { iTInf: k, iTSup: k };
    if (t < ts[k]) return { iTInf: k - 1, iTSup: k };
  }
  return { iTInf: n - 1, iTSup: n - 1 };
}

function getTIndex (index, boatName) {
   const theTime = getDateFromIndex (index, boatName);
   const diffHours = (theTime.getTime() - gribLimits.epochStart * 1000) / 3600000;
   const {iTInf, iTSup} = findTimeAround (diffHours);
   const iTime =  (gribLimits.timeStamps [iTSup] - diffHours) < (diffHours - gribLimits.timeStamps [iTInf]) ? iTSup : iTInf; 
   return iTime;
}

function getWindStride(zoom) {
  // Choose a coarser stride when zoomed out to reduce density
  if (zoom <= 4) return 6;
  if (zoom <= 6) return 4;
  if (zoom <= 8) return 3;
  return 2; // close zoom -> more detail
}

/**
 * Render wind barbs on the custom Leaflet canvas layer.
 *
 * This function draws a wind field using GRIB data on a canvas placed inside a
 * dedicated Leaflet pane. The canvas is automatically resized and aligned with
 * the current map viewport using geographic bounds (map.getBounds()).
 *
 * For each GRIB grid point, the function:
 *   1. Converts the (lat, lon) coordinate to a Leaflet layer point.
 *   2. Transforms it into the local canvas coordinate system.
 *   3. Applies optional thinning to avoid overdraw at low zoom levels.
 *   4. Draws a wind barb symbol representing U/V wind components.
 *
 * The canvas always stays visually below popups and markers, but above the base map.
 * Wind symbols update automatically when the map moves or zooms.
 *
 * Requirements:
 *   - `windCanvas` must be appended to a Leaflet pane via `map.createPane()`.
 *   - `dataGrib` must provide `getUVGW(timeIndex, latIndex, lonIndex)`.
 *   - `gribLimits` describes grid geometry (lat/lon steps, bounds, sizes).
 *   - `drawWindBarb(ctx, x, y, u, v)` must be defined elsewhere.
 *
 * Behavior:
 *   - Skips drawing points outside the visible map bounds.
 *   - Performs screen-space culling (one barb per 25px grid by default).
 *   - Reduces the number of barbs depending on map zoom (via `getWindStride()`).
 *
 * Called automatically from:
 *   `map.on('moveend zoomend resize', drawWind)`
 *
 * @function drawWind
 * @returns {void}
 */
function drawWind() {
  if (!route || !dataGrib || !dataGrib.getUVGW) return;
  const boatName = Object.keys(route)[0]; // Extract first key from response
  if (!route[boatName].track || route[boatName].track.length == 0
      || !dataGrib || !gribLimits || !gribLimits.timeStamps) return;

  const ctx = windCanvas.getContext('2d');
  const {
    nTimeStamp, nLat, nLon,
    bottomLat, topLat, leftLon, rightLon,
    latStep, lonStep, nShortName
  } = gribLimits;
  console.log ("nTimeStamp: ", nTimeStamp, "nShortName: ", nShortName);

  const len0 = nTimeStamp * nLat * nLon * nShortName; // u v g w
  const len1 = dataGrib.values.length;
  if (len0 != len1) {
      console.warn ("In drawWind: unconsistent dataGrib length");
      Swal.fire ("Unexpected grib size", `Value Length: ${len1}, Expected: ${len0},`, "error");
      return;
  }

  // 🔴 use visible geographic bounds
  const mapBounds = map.getBounds(); // LatLngBounds
  const topLeft     = map.latLngToLayerPoint(mapBounds.getNorthWest());
  const bottomRight = map.latLngToLayerPoint(mapBounds.getSouthEast());
  const size        = bottomRight.subtract(topLeft);

  // Positionner et dimensionner le canvas
  L.DomUtil.setPosition(windCanvas, topLeft);
  windCanvas.width  = size.x;
  windCanvas.height = size.y;

  ctx.clearRect(0, 0, windCanvas.width, windCanvas.height);

  const zoom = map.getZoom();
  const stride = getWindStride(zoom);

  const cellSize = 25; // px
  const usedCells = new Set();
  const iTimeStamp = getTIndex(index, boatName);

  for (let iLat = 0; iLat < nLat; iLat += stride) {
    const lat = bottomLat + iLat * latStep;

    for (let iLon = 0; iLon < nLon; iLon += stride) {
      const lon = leftLon + iLon * lonStep;

      // Optionnel : petit culling géographique pour ne pas traiter tout le globe
      if (lat < mapBounds.getSouth() - 1 || lat > mapBounds.getNorth() + 1 ||
          lon < mapBounds.getWest() - 1  || lon > mapBounds.getEast() + 1) {
        continue;
      }

      const { u, v } = dataGrib.getUVGW(iTimeStamp, iLat, iLon);
      const latLng   = L.latLng(lat, lon);

      // Coordonnées en layer
      const pt = map.latLngToLayerPoint(latLng);

      // Ramener dans le repère du canvas (origine = topLeft)
      const x = pt.x - topLeft.x;
      const y = pt.y - topLeft.y;

      // Culling écran
      if (x < 0 || y < 0 || x > windCanvas.width || y > windCanvas.height) continue;

      const cx = Math.floor(x / cellSize);
      const cy = Math.floor(y / cellSize);
      const key = cx + "," + cy;
      if (usedCells.has(key)) continue;
      usedCells.add(key);

      drawWindBarb(ctx, x, y, u, v);
    }
  }
}

/**
 * Draw a wind barb symbol at (x, y).
 * u, v are wind components in m/s (u: east-west, v: north-south).
 * speedMS in m/s is used to compute knots for barbs.
 */
function drawWindBarb(ctx, x, y, u, v) {
  let speedKts = Math.sqrt(u * u + v * v) * MS_TO_KN; // Knots

  // Very calm wind: draw a small circle only, no shaft
  if (!isFinite(speedKts) || speedKts < 2) {
    const r = 3; // radius in px
    ctx.beginPath();
    ctx.arc(x, y, r, 0, Math.PI * 2);
    ctx.stroke();
    return;
  }
  speedKts += 2.5;

  // Normalize direction
  const mag = Math.sqrt(u * u + v * v);
  if (mag < 0.5 || !isFinite(mag)) return;

  // On screen: x right, y down -> invert v to get proper direction
  const dirX = -u / mag;
  const dirY = v / mag;

  // Shaft parameters
  const halfShaft = 9; // px

  // Tail and head of the shaft (centered on (x, y))
  const tailX = x - dirX * halfShaft;
  const tailY = y - dirY * halfShaft;
  const headX = x + dirX * halfShaft;
  const headY = y + dirY * halfShaft;

  // Draw main shaft
  ctx.beginPath();
  ctx.moveTo(tailX, tailY);
  ctx.lineTo(headX, headY);
  ctx.stroke();

  // Barb parameters
  const barbLength = 7;    // full barb length in px
  const halfBarbLength = barbLength * 0.5;
  const barbSpacing = 4;   // spacing along shaft in px from the head backward

  // Perpendicular vector (to the right side of wind direction)
  const perpX = -dirY;
  const perpY = dirX;

  // Decompose speed into 50, 10 and 5 kt parts
  const n50 = Math.floor(speedKts / 50);   // number of 50 kt pennants
  let remainder = speedKts - n50 * 50;

  const n10 = Math.floor(remainder / 10);  // number of 10 kt barbs
  remainder -= n10 * 10;

  const has5 = remainder >= 5 ? 1 : 0;     // possible 5 kt half-barb

  // We place all features starting from the head, going backward.
  let currentOffset = 0;

  // --- 50 kt pennants (filled triangles) ---
  for (let i = 0; i < n50; i++) {
    // Base point of the pennant on the shaft
    const baseHeadX = headX - dirX * currentOffset;
    const baseHeadY = headY - dirY * currentOffset;

    // Point a bit behind along the shaft -> second base corner
    const baseTailX = baseHeadX - dirX * barbSpacing;
    const baseTailY = baseHeadY - dirY * barbSpacing;

    // Tip of the triangle, perpendicular to the shaft
    const tipX = baseTailX + perpX * barbLength;
    const tipY = baseTailY + perpY * barbLength;

    ctx.beginPath();
    ctx.moveTo(baseHeadX, baseHeadY);
    ctx.lineTo(baseTailX, baseTailY);
    ctx.lineTo(tipX, tipY);
    ctx.closePath();
    ctx.fill();   // filled pennant
    ctx.stroke(); // optional outline

    currentOffset += barbSpacing;
  }

  // --- 10 kt full barbs ---
  for (let i = 0; i < n10; i++) {
    const baseX = headX - dirX * currentOffset;
    const baseY = headY - dirY * currentOffset;

    const endX = baseX + perpX * barbLength;
    const endY = baseY + perpY * barbLength;

    ctx.beginPath();
    ctx.moveTo(baseX, baseY);
    ctx.lineTo(endX, endY);
    ctx.stroke();

    currentOffset += barbSpacing;
  }

  // --- 5 kt half-barb, if needed ---
  if (has5) {
    const baseX = headX - dirX * currentOffset;
    const baseY = headY - dirY * currentOffset;

    const endX = baseX + perpX * halfBarbLength;
    const endY = baseY + perpY * halfBarbLength;

    ctx.beginPath();
    ctx.moveTo(baseX, baseY);
    ctx.lineTo(endX, endY);
    ctx.stroke();
  }
}

