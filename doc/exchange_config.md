# Exchange config

Exchange needs following informations
* **module**: module (path)name - path to *.so file. Not required for simulator
* **options** : key-value flat structure with various options
* **credentials** : defines list of logins - multiple credentials can be defined. Each login
    has a name and definition of enabled accounts
    * **api_key** path to api key file (which is JSON)
    * **accounts** definition of accounts (see below)
* **instruments** list of instruments enabled for trading

```
{
    "exchanges": {
        "<name>": {
            "module": "<filename.so>",
            "options": {
                "key":"value",
                ...,
                ...,
            },
            "credentials": {
                "<label>":{
                    "api_key":"api-key-file.json",
                    "accounts":{
                        "<label>":{<query>}
                    }
                }
            },
            "instruments":{
                "<label>":{<query>}
            }
        }
}
```

## Querying accounts and instruments

To associate accounts and instruments with their abstract versions, you need to
somehow specify, how to find specified account or instrument at the exechange. And
this is "query" puprose

The "query" is any arbitrary JSON, which format depens on exchange. The result of
the "query" is any count of accounts (resp. instruments). All these results has
assigned the same label, unless the account (resp. instrument) has assigned a different
label by other query 
