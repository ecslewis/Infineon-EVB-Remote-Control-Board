# Portfolio site

Static multi-page site built from the content in `Portfolio.pptx`.
No build step, no dependencies — open `index.html` in a browser.

## Pages

| File | What it is |
|---|---|
| `index.html` | Home — hero, stats, 3 featured projects, skills |
| `projects.html` | Full project index with working filter buttons |
| `research.html` | MVS compact modelling, GaN HEMT research, publications |
| `about.html` | Bio, experience timeline, education |
| `contact.html` | Contact form + links |
| `project-*.html` | Six project detail pages |
| `project-evb.html` | **Blank template** — copy this for new projects |
| `images/` | PCB renders, schematics, scope captures |
| `style.css` | All styling. Theme colours are the CSS variables at the top |
| `scroll.js` | Scroll animation. Finds its own elements — no markup needed |
| `deploy.sh` | One-time GitHub Pages setup (already done) |
| `update.sh` | Publish changes to the live site — the one you'll keep using |

## Publishing changes

The site is live at **https://ecslewis.github.io**. To push updates:

```
bash ~/Documents/Infineon-EVB-Remote-Control-Board/portfolio/update.sh
```

Optionally with a message: `bash update.sh "added battery pack test results"`.
It copies this folder over `~/Documents/ecslewis.github.io`, commits and pushes.
Live about a minute later.

## Tuning the scroll animation

Three variables at the top of `style.css` control everything:

```css
--reveal-travel:   44px;    /* how far elements rise. 20px = subtle, 70px = dramatic */
--reveal-duration: 900ms;   /* how long. lower = snappier */
--reveal-ease:     cubic-bezier(0.16, 0.84, 0.28, 1);
```

`scroll.js` also does: a progress bar under the header, parallax on the hero and
figure images, count-up on numeric spec values, and re-reveal when the project
filters change. Anyone with "reduce motion" enabled in their OS gets a static site
automatically, and there's a `<noscript>` fallback on every page.

## Still to fill in

Content comes from your deck, resume, CSSTC abstract, presentation and design report.
Remaining gaps:

- [ ] **Hobbies paragraph** (`about.html`) — slide 20 of the deck promises it
- [ ] **STM32 alarm design doc** (`project-stm32-alarm.html`) — the report you sent turned
      out to be the Room Watchdog one, so this project still has no document linked
- [ ] **Scope capture captions** (`project-gan-platform.html`) — I wrote them from the
      instrument settings visible on screen; you know which node each probe was on and
      what mode the board was in, so tighten them
- [ ] **Contact form** — sign up at [formspree.io](https://formspree.io), paste your ID into `contact.html`

## Documents

Three PDFs are served from this folder and linked from the site:

| File | Linked from |
|---|---|
| `resume.pdf` | nav on every page, About sidebar |
| `csstc-2026-abstract.pdf` | Research page — "Read the abstract" |
| `room-watchdog-report.pdf` | Room Watchdog project — "Full design report" |
| `nand-flash-simulator.zip` | NAND project — "Download source" |

## Bug found in the NAND project

Your `Makefile` lists `ftl_main.c` in `FTL_SRCS`, but the file is called `ftlmain.c`.
Plain `make` builds `nand_sim` then dies with *"No rule to make target 'ftl_main.o'"*.
One-character fix:

```make
FTL_SRCS := ftlmain.c nand_flash.c ftl.c
```

Compiling the three files directly works fine, which is presumably how you've been
building it. Worth fixing before anyone clones it.

## Conflicts I resolved — check I picked right

| Thing | Deck says | Other source says | I used |
|---|---|---|---|
| Control MCU | dsPIC32GS502 | dsPIC33EP32GS502 (repo config) | repo value |
| GaN BDS rating | 850 V (slide 8) | 900 V (silkscreen + README) | 900 V |
| mmWave MCU | ESP32-WROOM | ESP32-S3 (resume) | ESP32-S3 |
| CSSTC year | implied 2025 | 2026 (resume) | 2026 |

Also note: your resume dates the Waterloo research as **Jan–May 2026** (finished), while
`research.html` describes some of it as ongoing. If it's wrapped up, change "Ongoing work"
in section 02 to past tense.

`resume.pdf` is in this folder and linked from every page's nav.

## Re-theming

Everything is driven by the variables at the top of `style.css`:

```css
--accent:     #35e0a1;   /* signal green — the main accent */
--accent-alt: #4cc9f0;   /* probe cyan — spec values */
--bg:         #0a0d10;   /* page background */
```

Change `--accent` and the whole site follows.

## Publishing

1. **GitHub Pages** — push this folder to a repo, Settings → Pages → deploy from branch.
2. **Netlify / Cloudflare Pages** — drag the folder onto their dashboard.
3. Point a custom domain at either.
