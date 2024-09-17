#include "check.h"
#include <quarkbot/ta/ema.h>
#include <quarkbot/ta/sma.h>
#include <quarkbot/ta/emstdev.h>
#include <quarkbot/ta/bbspread.h>


//static_assert(quarkbot::is_indicator<quarkbot::BBSpread<> >)
template class quarkbot::Persistent<quarkbot::BBSpread<> >;



int main()
{

}

