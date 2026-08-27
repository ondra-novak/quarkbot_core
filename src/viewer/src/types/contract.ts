import { InstrumentMeta } from "./report_types";

export function calculate_pnl(instr: InstrumentMeta, open :number, close: number, position: number) {
    const real_open = open * instr.tick_scale;
    const real_close = close * instr.tick_scale;
    const real_position = position * instr.multiplier;
    if (instr.type == "inverse_contract") {
        return real_position * (1.0/real_open - 1.0/real_close);
    } else {
        return real_position * (real_close - real_open);
    }
}

export function calculate_turnover(instr: InstrumentMeta,price :number, quantity: number) {
    const real_price = price * instr.tick_scale;
    const real_quantity = quantity * instr.multiplier;
    if (instr.type == "inverse_contract") {
        return real_quantity * (1.0/real_price);
    } else {
        return real_quantity * real_price;
    }


}