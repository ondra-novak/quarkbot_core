import gdb

class DecimalPrinter:
    """Pretty printer for quarkbot::Decimal."""

    def __init__(self, val):
        self.val = val

    def to_string(self):
        # Získání hodnoty _num_data
        num_data = int(self.val['_num_data'])
        
        # Výpočet m a e podle vzorce
        m = num_data // 256
        e = pow(10, (num_data % 256) - 127)
        
        # Výpočet výsledné hodnoty
        value = m * e
        
        # Vrácení hodnoty jako string
        return f"{value}"

def decimal_printer_lookup(val):
    """Lookup function pro zjištění, zda je proměnná instancí quarkbot::Decimal."""
    if "Decimal" in str(val.type):
        return DecimalPrinter(val)
    return None

# Registrace pretty-printeru v GDB
def register_decimal_printer():
    gdb.pretty_printers.append(decimal_printer_lookup)

register_decimal_printer()
