export type QLineStyle = "solid" | "double" | "dashed" | "area";

export interface SeriesSetupItem {
    color: string;
    line_style: QLineStyle;
};

export type SeriesSetup = Record<string, SeriesSetupItem>




export const paneTypes = ["main", "secondary", "pane_1", "pane_2"] as const;

export type PaneType = (typeof paneTypes)[number];


export type SeriesToPanes = Record<PaneType, string[]>;


export interface DisplayOptions  {
    instrument:string;
    interval: number;
    series_to_panes: SeriesToPanes;
    fills: boolean;
    orders: boolean;
    volume: boolean;
    setup: SeriesSetup;
};