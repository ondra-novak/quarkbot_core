import gdb

class DecimalPrinter:
    """Pretty-printer pro 64-bitový Decimal: 56b mantisa (0-1), 8b exponent"""

    def __init__(self, val):
        self.val = val
        # Načtení 64-bitové hodnoty
        self.packed = int(val['_packed'])

    def get_exponent(self):
        # 8-bitový signed exponent (posledních 8 bitů)
        exp = self.packed & 0xFF
        return exp if exp < 128 else exp - 256

    def get_mantissa(self):
        # Mantisa je horních 56 bitů (signed shift)
        # V Pythonu: (packed >> 8) zachová znaménko u 64-bit integeru
        return self.packed >> 8

    def to_string(self):
        exp = self.get_exponent()
        mant = self.get_mantissa()

        sci_mant = mant /10**15
        
        # Výpočet: mantisa / 1e16 (vytvoří 0.999...) * 10^exp
        # Používáme float pro zobrazení v debuggeru
        real_value = mant * (10**(exp-16))
        
      #  output = f'"{real_value:.16g} ( {sci_mant: .16g}e{exp-1} )"'
        
        # Formátování pro náhled v řádku (Summary)
        return real_value

    def children(self):
        # Rozbalovací položky pro okno "Variables"
        exp = self.get_exponent()
        mant = self.get_mantissa()
        return [
            ('value', (mant / 10**16) * (10**exp)),
            ('exponent', exp),
            ('mantissa', mant),
            ('raw_hex', hex(self.packed))
        ]

    def display_hint(self):
        # 'string' zajistí, že se to_string() zobrazí hned vedle názvu proměnné
        return 'string'

def register_decimal_printer(obj):
    if obj is None:
        obj = gdb.current_objfile()
    
    printer = gdb.printing.RegexpCollectionPrettyPrinter("DecimalLib")
    # Změňte '^Decimal$' na '^namespace::Decimal$', pokud je třída v namespace
    printer.add_printer('Decimal', '^Decimal$', DecimalPrinter)
    gdb.printing.register_pretty_printer(obj, printer)

register_decimal_printer(None)
