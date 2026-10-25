import { readFile, writeFile } from 'node:fs/promises';
import { resolve } from 'node:path';
import { pathToFileURL } from 'node:url';

const packageRoot = process.argv[2];
if (!packageRoot) throw new Error('Usage: node tools/avatar-masks/generate.mjs <material-shapes-ts package directory>');
const metadata = JSON.parse(await readFile(resolve(packageRoot, 'package.json'), 'utf8'));
if (metadata.name !== 'material-shapes-ts' || metadata.version !== '0.3.0') {
  throw new Error('Expected material-shapes-ts 0.3.0');
}
const { MaterialShapes, Morph } = await import(pathToFileURL(resolve(packageRoot, 'dist/index.js')));
const names = ['SoftBurst', 'Cookie9Sided', 'Pentagon', 'Pill', 'Sunny', 'Cookie4Sided', 'Oval'];
const shapes = names.map(name => MaterialShapes[name].normalized());
const scale = Math.min(...shapes.map(shape => {
  const bounds = shape.calculateBounds();
  const max = shape.calculateMaxBounds();
  return Math.max((bounds[2] - bounds[0]) / (max[2] - max[0]), (bounds[3] - bounds[1]) / (max[3] - max[1]));
}));
const cubics = [], transitions = [];
for (let i = 0; i < shapes.length; ++i) {
  const morph = new Morph(shapes[i], shapes[(i + 1) % shapes.length]);
  const start = morph.asCubics(0), end = morph.asCubics(1);
  if (start.length !== end.length) throw new Error('Unmatched curves');
  const begin = cubics.length;
  start.forEach((curve, index) => {
    const points = [...curve.points, ...end[index].points];
    if (points.length !== 16 || !points.every(Number.isFinite)) throw new Error('Invalid curve');
    cubics.push(`\t{ { ${curve.points.join(', ')} }, { ${end[index].points.join(', ')} } },`);
  });
  transitions.push(`\t{ ${begin}, ${cubics.length} },`);
}
const result = `/* Android Open Source Project 2022–2024; material-shapes-ts 0.3.0. Apache-2.0, see LEGAL. */
#pragma once

#include <array>

namespace Ui::ExpressiveAvatarGeometry {

struct Cubic {
\tstd::array<double, 8> start;
\tstd::array<double, 8> end;
};

struct Transition {
\tint begin = 0;
\tint end = 0;
};

inline constexpr auto kScale = ${scale};
inline constexpr auto kCubics = std::array<Cubic, ${cubics.length}>{{
${cubics.join('\n')}
}};
inline constexpr auto kTransitions = std::array<Transition, ${transitions.length}>{{
${transitions.join('\n')}
}};

} // namespace Ui::ExpressiveAvatarGeometry
`;
const destination = new URL('../../Telegram/SourceFiles/ui/effects/expressive_avatar_geometry.h', import.meta.url);
await writeFile(destination, result.replace(/\r?\n/g, '\r\n'));
console.log(`Generated ${cubics.length} matched curves across ${transitions.length} transitions.`);
