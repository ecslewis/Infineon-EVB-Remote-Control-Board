/* ==========================================================================
   Scroll animation
   --------------------------------------------------------------------------
   No HTML changes needed — this file finds the elements itself and tags them.
   Everything respects prefers-reduced-motion.

   To dial the whole effect up or down, change --reveal-travel and
   --reveal-duration at the top of style.css.
   ========================================================================== */

(function () {
  "use strict";

  var reduced = window.matchMedia("(prefers-reduced-motion: reduce)").matches;

  /* ---------------------------------------------------------------------
     1. SCROLL PROGRESS BAR
     A hairline under the sticky header that fills as you move down the page.
     --------------------------------------------------------------------- */
  var bar = document.createElement("div");
  bar.className = "scroll-progress";
  document.body.appendChild(bar);

  /* ---------------------------------------------------------------------
     2. TAG ELEMENTS FOR REVEAL
     Each selector gets a reveal style. Groups get a stagger so siblings
     arrive one after another instead of all at once.
     --------------------------------------------------------------------- */
  var plan = [
    { sel: ".hero .kicker",        anim: "rise" },
    { sel: ".hero h1",             anim: "rise" },
    { sel: ".hero p.lead",         anim: "rise" },
    { sel: ".hero .btn-row",       anim: "rise" },
    { sel: ".hero-figure",         anim: "zoom" },
    { sel: ".spec-strip",          anim: "rise" },
    { sel: ".section-head",        anim: "sweep" },
    { sel: "section p",            anim: "rise" },
    { sel: ".spec-table",          anim: "rise" },
    { sel: ".note",                anim: "sweep" },
    { sel: ".skill-group",         anim: "rise" },
    { sel: ".timeline .tl-item",   anim: "sweep" },
    { sel: ".contact-list li",     anim: "sweep" },
    { sel: "form",                 anim: "rise" },
    { sel: ".filters",             anim: "rise" },
    { sel: "aside .panel",         anim: "rise" },
    { sel: "figure",               anim: "zoom" },
    { sel: ".grid > .card",        anim: "zoom", stagger: 90 }
  ];

  var seen = new Set();
  var targets = [];

  plan.forEach(function (rule) {
    var els = document.querySelectorAll(rule.sel);
    els.forEach(function (el, i) {
      if (seen.has(el)) return;          // don't double-tag nested matches
      seen.add(el);
      el.classList.add("reveal", "reveal--" + rule.anim);
      if (rule.stagger) {
        el.style.transitionDelay = (i % 6) * rule.stagger + "ms";
      }
      targets.push(el);
    });
  });

  /* Elements already in view on load shouldn't animate in — they should just
     be there. Except in the hero, where the entrance is the point. */
  function inViewAtLoad(el) {
    var r = el.getBoundingClientRect();
    return r.top < window.innerHeight * 0.92 && r.bottom > 0;
  }

  if (reduced) {
    targets.forEach(function (el) { el.classList.add("is-visible"); });
    return;
  }

  /* ---------------------------------------------------------------------
     3. REVEAL ON SCROLL
     --------------------------------------------------------------------- */
  var observer = new IntersectionObserver(function (entries) {
    entries.forEach(function (entry) {
      if (entry.isIntersecting) {
        entry.target.classList.add("is-visible");
        observer.unobserve(entry.target);   // animate once, then leave it alone
      }
    });
  }, {
    rootMargin: "0px 0px -12% 0px",   // fire slightly before it hits the bottom edge
    threshold: 0.08
  });

  targets.forEach(function (el) { observer.observe(el); });

  /* Hero content animates in on load rather than waiting for a scroll. */
  var heroEls = document.querySelectorAll(".hero .reveal");
  heroEls.forEach(function (el, i) {
    el.style.transitionDelay = 80 + i * 110 + "ms";
  });
  requestAnimationFrame(function () {
    requestAnimationFrame(function () {
      heroEls.forEach(function (el) { el.classList.add("is-visible"); });
    });
  });

  /* ---------------------------------------------------------------------
     4. PARALLAX + PROGRESS (single rAF-throttled scroll handler)
     --------------------------------------------------------------------- */
  var heroFigure = document.querySelector(".hero-figure img");
  var figureImgs = Array.prototype.slice.call(
    document.querySelectorAll("figure .shot:not(.pad) img, .card-media img")
  );
  var ticking = false;

  function onScroll() {
    if (ticking) return;
    ticking = true;

    requestAnimationFrame(function () {
      var y = window.scrollY || window.pageYOffset;
      var doc = document.documentElement.scrollHeight - window.innerHeight;

      /* progress bar */
      bar.style.transform = "scaleX(" + (doc > 0 ? y / doc : 0) + ")";

      /* hero image drifts slower than the page */
      if (heroFigure && y < window.innerHeight * 1.5) {
        heroFigure.style.transform = "translate3d(0," + y * 0.12 + "px,0) scale(1.06)";
      }

      /* figures drift gently within their frame as they cross the viewport */
      figureImgs.forEach(function (img) {
        var r = img.getBoundingClientRect();
        if (r.bottom < -200 || r.top > window.innerHeight + 200) return;
        var mid = (r.top + r.height / 2 - window.innerHeight / 2) / window.innerHeight;
        img.style.setProperty("--drift", (mid * -14).toFixed(2) + "px");
      });

      ticking = false;
    });
  }

  window.addEventListener("scroll", onScroll, { passive: true });
  window.addEventListener("resize", onScroll, { passive: true });
  onScroll();

  /* ---------------------------------------------------------------------
     5. COUNT-UP ON NUMERIC SPEC VALUES
     Only touches values that are actually numbers — "GaN Power Electronics"
     is left alone, "1.04 ns" and "6+" count up.
     --------------------------------------------------------------------- */
  var numRe = /^([^\d\-]*)(-?\d+(?:\.\d+)?)(.*)$/;

  document.querySelectorAll(".spec .v").forEach(function (el) {
    var m = el.textContent.trim().match(numRe);
    if (!m) return;

    var pre = m[1], target = parseFloat(m[2]), post = m[3];
    var decimals = (m[2].split(".")[1] || "").length;
    var started = false;

    var io = new IntersectionObserver(function (entries) {
      entries.forEach(function (e) {
        if (!e.isIntersecting || started) return;
        started = true;
        io.disconnect();

        var dur = 1100, t0 = null;
        function step(ts) {
          if (!t0) t0 = ts;
          var p = Math.min((ts - t0) / dur, 1);
          var eased = 1 - Math.pow(1 - p, 3);          // ease-out cubic
          el.textContent = pre + (target * eased).toFixed(decimals) + post;
          if (p < 1) requestAnimationFrame(step);
        }
        requestAnimationFrame(step);
      });
    }, { threshold: 0.5 });

    io.observe(el);
  });

  /* ---------------------------------------------------------------------
     6. RE-REVEAL FILTERED CARDS
     When the projects filter runs, cards that come back into the grid
     should animate in rather than pop.
     --------------------------------------------------------------------- */
  document.querySelectorAll(".filter").forEach(function (btn) {
    btn.addEventListener("click", function () {
      document.querySelectorAll("#project-grid .card").forEach(function (card, i) {
        if (card.classList.contains("hidden")) return;
        card.classList.remove("is-visible");
        card.style.transitionDelay = (i % 6) * 60 + "ms";
        requestAnimationFrame(function () {
          requestAnimationFrame(function () { card.classList.add("is-visible"); });
        });
      });
    });
  });

})();
