# Generic configuration 

## exchanges 
list of exchanges
- name (label)
- module path
- generic settings (key-value, subsection)


## profiles
profiles is separate section where each defines credentials and accounts

- name of profile
- associated exchange
- credentials (api key)
    - key-value fields
## accounts
list of accounts (multiple accounts can be result of single definition)
 - name of account (label)
 - profile name
 - query (how to retrieve account from profile) - key-value fields

## instruments
list of instruments (multiple instruments can be result of single definition)
 - name of instrument (label)
 - profile name (optional)
 - exchange name  (optional)
 - query (how to retrieve account from profile) - key-value fields
 
## strategies
list of strategies 
 - name (label)
 - module path
 - generic settings (key-value)  
 - instruments: comma separated labels
 - accounts: comma separated labels