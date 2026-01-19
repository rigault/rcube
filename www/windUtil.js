/**
 * Return bounding GRIB indices around a time in hours:
 * gribLimits.timeStamps[iTInf] <= tHours <= gribLimits.timeStamps[iTSup]
 *
 * @param {number} tHours
 * @returns {{iTInf:number, iTSup:number}}
 */
function findTimeAround(tHours) {
  const ts = gribLimits.timeStamps;
  const n = gribLimits.nTimeStamp;

  if (!Number.isFinite(tHours) || tHours < ts[0]) return { iTInf: 0, iTSup: 0 };

  for (let k = 0; k < n; k++) {
    if (tHours === ts[k]) return { iTInf: k, iTSup: k };
    if (tHours < ts[k]) return { iTInf: k - 1, iTSup: k };
  }
  return { iTInf: n - 1, iTSup: n - 1 };
}

/**
 * Choose a coarser stride when zoomed out to reduce density.
 * @param {number} zoom
 * @returns {number}
 */
function getWindStride(zoom) {
  if (zoom <= 4) return 6;
  if (zoom <= 6) return 4;
  if (zoom <= 8) return 3;
  return 2;
}

/**
 * Draw a wind barb symbol at (x, y).
 * u, v are wind components in m/s (u: east-west, v: north-south).
 *
 * @param {CanvasRenderingContext2D} ctx
 * @param {number} x
 * @param {number} y
 * @param {number} u
 * @param {number} v
 * @returns {void}
 */
function drawWindBarb(ctx, x, y, u, v) {
  let speedKts = Math.sqrt(u * u + v * v) * MS_TO_KN;

  // Calm wind: draw a small circle
  if (!isFinite(speedKts) || speedKts < 2) {
    const r = 3;
    ctx.beginPath();
    ctx.arc(x, y, r, 0, Math.PI * 2);
    ctx.stroke();
    return;
  }

  // Small visual bias (kept from original implementation)
  speedKts += 2.5;

  const mag = Math.sqrt(u * u + v * v);
  if (mag < 0.5 || !isFinite(mag)) return;

  // On screen: x right, y down
  const dirX = -u / mag;
  const dirY = v / mag;

  const halfShaft = 9;
  const tailX = x - dirX * halfShaft;
  const tailY = y - dirY * halfShaft;
  const headX = x + dirX * halfShaft;
  const headY = y + dirY * halfShaft;

  ctx.beginPath();
  ctx.moveTo(tailX, tailY);
  ctx.lineTo(headX, headY);
  ctx.stroke();

  const barbLength = 7;
  const halfBarbLength = barbLength * 0.5;
  const barbSpacing = 4;

  const perpX = -dirY;
  const perpY = dirX;

  const n50 = Math.floor(speedKts / 50);
  let remainder = speedKts - n50 * 50;

  const n10 = Math.floor(remainder / 10);
  remainder -= n10 * 10;

  const has5 = remainder >= 5 ? 1 : 0;

  let currentOffset = 0;

  // 50 kt pennants
  for (let i = 0; i < n50; i++) {
    const baseHeadX = headX - dirX * currentOffset;
    const baseHeadY = headY - dirY * currentOffset;

    const baseTailX = baseHeadX - dirX * barbSpacing;
    const baseTailY = baseHeadY - dirY * barbSpacing;

    const tipX = baseTailX + perpX * barbLength;
    const tipY = baseTailY + perpY * barbLength;

    ctx.beginPath();
    ctx.moveTo(baseHeadX, baseHeadY);
    ctx.lineTo(baseTailX, baseTailY);
    ctx.lineTo(tipX, tipY);
    ctx.closePath();
    ctx.fill();
    ctx.stroke();

    currentOffset += barbSpacing;
  }

  // 10 kt barbs
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

  // 5 kt half-barb
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
