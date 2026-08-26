interface ColorOptions {
  saturation?: number; // v procentech (0-100)
  lightness?: number;  // v procentech (0-100)
}

/**
 * Vygeneruje stabilní HSL barvu na základě zadaného řetězce.
 * Pokud je zadán prázdný řetězec, vrátí výchozí barvu.
 */
export function stringToColor(str: string, options?: ColorOptions): string {
  const saturation = options?.saturation ?? 70; // 70% pro živé barvy
  const lightness = options?.lightness ?? 40;   // 40% pro optimální kontrast

  if (!str) {
    return `hsl(0, 0%, ${lightness}%)`; // Výchozí šedá barva pro prázdný řetězec
  }

  let hash = 0;
  for (let i = 0; i < str.length; i++) {
    // Klasický djb2 hash algoritmus pro rovnoměrné rozložení
    hash = str.charCodeAt(i) + ((hash << 5) - hash);
  }

  // Odstín (Hue) je v rozsahu 0-360 stupňů
  const hue = Math.abs(hash) % 360;

  return `hsl(${hue}, ${saturation}%, ${lightness}%)`;
}
