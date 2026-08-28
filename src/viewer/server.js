#!/usr/bin/env node
import http from 'http'
import fs from 'fs'
import os from 'os'
import path from 'path'
import { fileURLToPath } from 'url'

const __dirname = path.dirname(fileURLToPath(import.meta.url))
const distDir = path.join(__dirname, 'dist')

// ---------------------------------------------------------------- arguments

// Přidání nového přepínače = jeden řádek zde; usage se generuje z této tabulky.
const SPEC = {
  port: { short: '-p', long: '--port', arg: 'PORT', help: 'HTTP port (default: a free port picked by the OS)' },
  profile: { short: '-P', long: '--profile', arg: 'PATH', help: 'options profile file (default: $XDG_CONFIG_HOME/quarkbot_viewer/options.json)' },
  no_save: { long: '--no-save', help: 'serve read-only: do not persist viewer options' },
  help: { short: '-h', long: '--help', help: 'show this help' },
}

function usage() {
  const lines = Object.values(SPEC).map(d => {
    const flags = [d.short, d.long].filter(Boolean).join(', ')
    return `  ${(flags + (d.arg ? ` ${d.arg}` : '')).padEnd(26)}${d.help}`
  })
  return `Usage: node server.js <path-to-report.jsonl> [options]\n\n${lines.join('\n')}\n`
}

function die(msg) {
  console.error(`${msg}\n\n${usage()}`)
  process.exit(1)
}

function parse_args(argv, spec) {
  const by_flag = new Map()
  for (const [name, d] of Object.entries(spec)) {
    if (d.short) by_flag.set(d.short, [name, d])
    if (d.long) by_flag.set(d.long, [name, d])
  }

  const flags = {}
  const positional = []

  for (let i = 0; i < argv.length; ++i) {
    const a = argv[i]
    if (a === '--') {
      positional.push(...argv.slice(i + 1))
      break
    }
    if (a === '-' || !a.startsWith('-')) {
      positional.push(a)
      continue
    }

    const eq = a.indexOf('=')
    const key = eq === -1 ? a : a.slice(0, eq)
    const entry = by_flag.get(key)
    if (!entry) die(`Unknown option: ${key}`)
    const [name, d] = entry

    if (!d.arg) {
      if (eq !== -1) die(`Option ${key} takes no value`)
      flags[name] = true
      continue
    }
    if (eq !== -1) {
      flags[name] = a.slice(eq + 1)
    } else {
      if (argv[i + 1] === undefined) die(`Option ${key} requires ${d.arg}`)
      flags[name] = argv[++i]
    }
  }

  return { flags, positional }
}

const { flags, positional } = parse_args(process.argv.slice(2), SPEC)

if (flags.help) {
  console.log(usage())
  process.exit(0)
}

let port = 0
if (flags.port !== undefined) {
  port = Number.parseInt(flags.port, 10)
  if (!Number.isInteger(port) || port < 0 || port > 65535) die(`Invalid port: ${flags.port}`)
}

const reportPath = positional[0]
if (!reportPath) die('Missing report file.')
if (positional.length > 1) die(`Unexpected extra argument: ${positional[1]}`)
if (!fs.existsSync(reportPath)) die(`File not found: ${reportPath}`)

/** --profile > $QUARKBOT_VIEWER_PROFILE > $XDG_CONFIG_HOME > ~/.config */
function resolve_profile(explicit) {
  if (explicit) return path.resolve(explicit)
  if (process.env.QUARKBOT_VIEWER_PROFILE) return path.resolve(process.env.QUARKBOT_VIEWER_PROFILE)
  const base = process.env.XDG_CONFIG_HOME || path.join(os.homedir(), '.config')
  return path.join(base, 'quarkbot_viewer', 'options.json')
}

const profilePath = resolve_profile(flags.profile)
const saveEnabled = !flags.no_save

// ---------------------------------------------------------------- options io

const MAX_OPTIONS_BYTES = 1024 * 1024

function read_body(req, limit) {
  return new Promise((resolve, reject) => {
    const chunks = []
    let size = 0
    let over = false
    req.on('data', chunk => {
      if (over) return
      size += chunk.length
      if (size > limit) {
        // odpověď musí odejít dřív, než se tělo zahodí, jinak klient
        // (např. curl s Expect: 100-continue) neuvidí stavový kód
        over = true
        reject(Object.assign(new Error('Payload too large'), { code: 'TOO_LARGE' }))
        return
      }
      chunks.push(chunk)
    })
    req.on('end', () => resolve(Buffer.concat(chunks).toString('utf8')))
    req.on('error', reject)
  })
}

function send_options(res) {
  let text
  try {
    text = fs.readFileSync(profilePath, 'utf8')
    JSON.parse(text)  // poškozený profil nesmí zablokovat viewer
  } catch (err) {
    if (err.code !== 'ENOENT') {
      console.warn(`Ignoring unreadable profile ${profilePath}: ${err.message}`)
    }
    res.writeHead(204)  // profil zatím není - klient si vezme výchozí
    res.end()
    return
  }
  res.writeHead(200, {
    'Content-Type': 'application/json; charset=utf-8',
    'Content-Length': Buffer.byteLength(text),
    'Cache-Control': 'no-store',
  })
  res.end(text)
}

async function store_options(req, res) {
  if (!saveEnabled) {
    res.writeHead(204)
    res.end()
    return
  }

  let body
  try {
    body = await read_body(req, MAX_OPTIONS_BYTES)
  } catch (err) {
    res.writeHead(err.code === 'TOO_LARGE' ? 413 : 400, { 'Content-Type': 'text/plain' })
    res.end(err.message)
    req.resume()  // zbytek těla dočíst a zahodit, ať socket nezůstane viset
    return
  }

  let parsed
  try {
    parsed = JSON.parse(body)
  } catch (err) {
    res.writeHead(400, { 'Content-Type': 'text/plain' })
    res.end(`Invalid JSON: ${err.message}`)
    return
  }
  if (parsed === null || typeof parsed !== 'object' || Array.isArray(parsed)) {
    res.writeHead(400, { 'Content-Type': 'text/plain' })
    res.end('Expected a JSON object')
    return
  }

  // atomicky: ukládá se při každé změně, takže odseknutý zápis by profil zahodil
  const tmp = `${profilePath}.${process.pid}.tmp`
  try {
    fs.mkdirSync(path.dirname(profilePath), { recursive: true })
    fs.writeFileSync(tmp, JSON.stringify(parsed, null, 2))
    fs.renameSync(tmp, profilePath)
  } catch (err) {
    try { fs.unlinkSync(tmp) } catch { /* nic */ }
    console.error(`Failed to write profile ${profilePath}: ${err.message}`)
    res.writeHead(500, { 'Content-Type': 'text/plain' })
    res.end('Failed to write profile')
    return
  }

  res.writeHead(204)
  res.end()
}

// ---------------------------------------------------------------- http

const MIME = {
  '.html': 'text/html',
  '.js': 'application/javascript',
  '.css': 'text/css',
  '.svg': 'image/svg+xml',
  '.ico': 'image/x-icon',
  '.woff2': 'font/woff2',
}

const server = http.createServer((req, res) => {
  const urlPath = req.url?.split('?')[0] ?? '/'

  if (urlPath === '/api/report') {
    const stat = fs.statSync(reportPath)
    res.writeHead(200, {
      'Content-Type': 'text/plain; charset=utf-8',
      'Content-Length': stat.size,
      'Cache-Control': 'no-cache',
    })
    fs.createReadStream(reportPath).pipe(res)
    return
  }

  if (urlPath === '/api/options') {
    if (req.method === 'GET') {
      send_options(res)
    } else if (req.method === 'PUT') {
      store_options(req, res)
    } else {
      res.writeHead(405, { 'Allow': 'GET, PUT' })
      res.end()
    }
    return
  }

  const filePath = path.join(distDir, urlPath === '/' ? 'index.html' : urlPath)
  const ext = path.extname(filePath)

  if (fs.existsSync(filePath) && fs.statSync(filePath).isFile()) {
    res.writeHead(200, { 'Content-Type': MIME[ext] ?? 'application/octet-stream' })
    fs.createReadStream(filePath).pipe(res)
  } else {
    // SPA fallback — serve index.html for client-side routing
    const indexPath = path.join(distDir, 'index.html')
    if (fs.existsSync(indexPath)) {
      res.writeHead(200, { 'Content-Type': 'text/html' })
      fs.createReadStream(indexPath).pipe(res)
    } else {
      res.writeHead(404)
      res.end('Build not found. Run: npm run build')
    }
  }
})

server.on('error', (err) => {
  if (err.code === 'EADDRINUSE') {
    console.error(`Port ${port} is already in use. Try without -p to use a random port.`)
  } else {
    console.error(err.message)
  }
  process.exit(1)
})

server.listen(port, () => {
  const { port: actualPort } = server.address()
  console.log(`QuarkBot Report Viewer → http://localhost:${actualPort}`)
  console.log(`Report:  ${path.resolve(reportPath)}`)
  console.log(`Profile: ${profilePath}${saveEnabled ? '' : ' (read-only, --no-save)'}`)
})
