History files
```
- .csv.gz

```

ini declaration

```

[history]
type=ohlc|close|auction|trade|quote|l1
interval=m|h|d
index=index.csv 


```

# index
Index contains two column
```
symbol,file
```

supported files are .csv, or .csv.gz



# types

## ohlc
```
time,open,high,low,close,volume
```
## close/trade
```
time,close,volume
```
### auction
```
time,open_price,open_volume,close_price,close_volume
```
### quote
```
time,ask_price,ask_volume,bid_price,bid_volume
```

### l1

```
time,ask_price,ask_volume,bid_price,bid_volume, price, volume
```

note - price is set to zero, if there is no trade


