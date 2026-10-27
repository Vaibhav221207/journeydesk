/* Journey Desk: same-origin, dependency-free browser client. Reservation rules live in C++. */
"use strict";

(() => {
  const $ = (id) => document.getElementById(id);
  const state = {
    catalog: null,
    mode: "Bus",
    view: "home",
    selected: null,
    journey: null,
    passengers: [{ uid: 1, name: "", age: "", gender: "", seat_number: null }],
    nextPassengerId: 2,
    activePassenger: 1,
    reviewing: null,
    receipt: null,
    receiptVersion: 0,
    ticket: null,
    demo: false,
    demoReady: false,
    deskKey: "",
    deskSnapshot: null,
    busy: false,
    reads: new Map(),
    sequence: 0,
    disabledBeforeMutation: new Map()
  };

  function node(tag, className, text) {
    const element = document.createElement(tag);
    if (className) element.className = className;
    if (text !== undefined && text !== null) element.textContent = String(text);
    return element;
  }

  function icon(name) {
    const svg = document.createElementNS("http://www.w3.org/2000/svg", "svg");
    svg.setAttribute("class", "icon");
    svg.setAttribute("aria-hidden", "true");
    const use = document.createElementNS("http://www.w3.org/2000/svg", "use");
    use.setAttribute("href", `#i-${name}`);
    svg.append(use);
    return svg;
  }

  function action(text, className, handler, iconName) {
    const button = node("button", className, text);
    button.type = "button";
    if (iconName) button.append(icon(iconName));
    button.addEventListener("click", handler);
    return button;
  }

  const money = (amount) => new Intl.NumberFormat("en-IN", {
    style: "currency", currency: "INR", maximumFractionDigits: 0
  }).format(amount);

  function dateLabel(date, compact = false) {
    const parsed = new Date(`${date}T12:00:00`);
    if (Number.isNaN(parsed.getTime())) return date || "";
    return parsed.toLocaleDateString("en-IN", compact
      ? { day: "numeric", month: "short", year: "numeric" }
      : { weekday: "long", day: "numeric", month: "long", year: "numeric" });
  }

  function duration(minutes) {
    const hours = Math.floor(minutes / 60);
    const rest = minutes % 60;
    return `${hours}h${rest ? ` ${rest}m` : ""}`;
  }

  function statusBadge(status) {
    return node("span", `status ${status.toLowerCase()}`, status);
  }

  function dataCell(label, value) {
    const cell = node("div", "data-cell");
    cell.append(node("dt", "", label), node("dd", "", value));
    return cell;
  }

  function setHeading(id, first, emphasized) {
    $(id).replaceChildren(document.createTextNode(first), node("em", "", emphasized));
  }

  function cancelReads(except) {
    for (const [key, request] of state.reads) {
      if (key !== except) {
        request.controller.abort();
        state.reads.delete(key);
      }
    }
  }

  class ApiError extends Error {
    constructor(message, status = 0, fields = {}) {
      super(message);
      this.status = status;
      this.fields = fields;
    }
  }

  async function request(path, options = {}) {
    let response;
    try {
      response = await fetch(path, { cache: "no-store", credentials: "same-origin", ...options });
    } catch (error) {
      if (error.name === "AbortError") throw error;
      throw new ApiError("We couldn’t reach the reservation server. Check that it is running, then try again.");
    }
    let result;
    try { result = await response.json(); }
    catch (error) {
      if (error.name === "AbortError") throw error;
      throw new ApiError("The server returned an unreadable response. Please try again.", response.status);
    }
    if (!response.ok) throw new ApiError(result.error || "The request could not be completed.", response.status, result.fields || {});
    return result;
  }

  async function latest(channel, path, options = {}) {
    state.reads.get(channel)?.controller.abort();
    const controller = new AbortController();
    const sequence = ++state.sequence;
    state.reads.set(channel, { controller, sequence });
    try {
      const result = await request(path, { ...options, signal: controller.signal });
      if (state.reads.get(channel)?.sequence !== sequence) throw new DOMException("Superseded request", "AbortError");
      return result;
    } catch (error) {
      if (controller.signal.aborted || state.reads.get(channel)?.sequence !== sequence) throw new DOMException("Superseded request", "AbortError");
      throw error;
    } finally {
      if (state.reads.get(channel)?.sequence === sequence) state.reads.delete(channel);
    }
  }

  function postOptions(body, headers = {}) {
    return { method: "POST", headers: { "Content-Type": "application/json", ...headers }, body: JSON.stringify(body) };
  }

  function queryPath(path, values) {
    return `${path}?${new URLSearchParams(values).toString()}`;
  }

  function setMutationBusy(busy) {
    state.busy = busy;
    document.body.setAttribute("aria-busy", String(busy));
    if (busy) {
      state.disabledBeforeMutation.clear();
      for (const control of document.querySelectorAll("button,input,select")) {
        state.disabledBeforeMutation.set(control, control.disabled);
        control.disabled = true;
      }
    } else {
      for (const [control, wasDisabled] of state.disabledBeforeMutation) {
        if (control.isConnected) control.disabled = wasDisabled;
      }
      state.disabledBeforeMutation.clear();
    }
  }

  async function mutate(path, body, headers) {
    if (state.busy) throw new ApiError("A reservation update is already in progress.");
    cancelReads("catalog");
    setMutationBusy(true);
    try { return await request(path, postOptions(body, headers)); }
    finally { setMutationBusy(false); }
  }

  function formBusy(form, busy) {
    form.setAttribute("aria-busy", String(busy));
    for (const button of form.querySelectorAll('button[type="submit"]')) button.disabled = busy;
  }

  function show(view, nav = view, focus = true) {
    cancelReads("catalog");
    state.view = view;
    document.body.dataset.view = view;
    for (const section of document.querySelectorAll("main>.view")) section.hidden = section.id !== `${view}-view`;
    for (const link of document.querySelectorAll(".main-nav [data-nav]")) {
      const active = link.dataset.nav === nav;
      link.classList.toggle("active", active);
      if (active) link.setAttribute("aria-current", "page");
      else link.removeAttribute("aria-current");
    }
    if (focus) {
      window.scrollTo({ top: 0, behavior: "instant" });
      const heading = $(`${view}-view`).querySelector("h1");
      if (heading) { heading.tabIndex = -1; heading.focus({ preventScroll: true }); }
    }
  }

  function notice(id, message) {
    $(id).textContent = message;
    $(id).hidden = !message;
  }

  function clearErrors(formId, summaryId) {
    const form = $(formId);
    for (const field of form.querySelectorAll("[aria-invalid]")) field.removeAttribute("aria-invalid");
    for (const error of form.querySelectorAll(".field-error")) error.textContent = "";
    if (summaryId) notice(summaryId, "");
  }

  function fieldError(id, message) {
    const input = $(id);
    const error = $(`${id}-error`);
    if (!input || !error) return;
    input.setAttribute("aria-invalid", "true");
    const descriptions = new Set((input.getAttribute("aria-describedby") || "").split(" ").filter(Boolean));
    descriptions.add(error.id);
    input.setAttribute("aria-describedby", [...descriptions].join(" "));
    error.textContent = message;
  }

  function fail(id, error, fieldMap = {}) {
    if (error.name === "AbortError") return;
    notice(id, error.message || "Please try again.");
    for (const [field, message] of Object.entries(error.fields || {})) {
      const target = fieldMap[field];
      if (target) fieldError(target, message);
    }
    $(id).focus();
  }

  function validationError(summaryId, fields) {
    if (!Object.keys(fields).length) return false;
    for (const [id, message] of Object.entries(fields)) fieldError(id, message);
    notice(summaryId, "Please check these details and try again.");
    const list = node("ul", "error-links");
    for (const [id, message] of Object.entries(fields)) {
      const item = node("li");
      const link = node("a", "", message);
      link.href = `#${id}`;
      link.addEventListener("click", (event) => { event.preventDefault(); $(id)?.focus(); });
      item.append(link);
      list.append(item);
    }
    $(summaryId).append(list);
    $(summaryId).focus();
    return true;
  }

  function emptyState(title, message) {
    const box = node("div", "empty-state");
    box.append(node("h3", "", title), node("p", "", message));
    return box;
  }

  function loading(text) {
    return node("div", "empty-state loading-line", text);
  }

  function offerSeatRetry() {
    const selected = { ...state.selected };
    $("passenger-error").append(node("br"), action("Try loading seats again", "text-button", () => chooseJourney(selected.service_id, selected.date, selected.demo)));
  }

  function serviceName(service) {
    const container = node("div", "service-name");
    const symbol = node("div", "service-mode-icon");
    symbol.append(icon(service.mode === "Bus" ? "bus" : "train"));
    const copy = node("div");
    copy.append(node("h3", "", service.name), node("p", "", `${service.number} · ${service.mode} · Direct`));
    container.append(symbol, copy);
    return container;
  }

  function serviceTimes(service) {
    const times = node("div", "service-times");
    const departure = node("div", "time-point");
    departure.append(node("strong", "", service.departure), node("span", "", service.source));
    const line = node("div", "travel-line");
    line.append(node("small", "", duration(service.duration_minutes)), node("span"));
    const arrival = node("div", "time-point");
    arrival.append(node("strong", "", service.arrival), node("span", "", service.destination));
    if (service.arrival_day_offset) arrival.append(node("span", "arrival-next", `+${service.arrival_day_offset} day${service.arrival_day_offset > 1 ? "s" : ""}`));
    times.append(departure, line, arrival);
    return times;
  }

  function journeyStrip(containerId, journey) {
    const date = node("div", "journey-date", dateLabel(journey.date, true));
    date.append(node("small", "", journey.demo ? "Fictional demonstration" : "Sample timetable"));
    $(containerId).replaceChildren(serviceName(journey.service), serviceTimes(journey.service), date);
  }

  function resetSearchResults() {
    state.reads.get("search")?.controller.abort();
    state.reads.delete("search");
    $("search-results").hidden = true;
    $("service-list").replaceChildren();
    $("search-button").disabled = !state.catalog;
    $("search-form").setAttribute("aria-busy", "false");
  }

  function changeMode(mode) {
    if (state.busy) return;
    state.mode = mode;
    for (const item of ["Bus", "Train"]) {
      const selected = item === mode;
      $(`mode-${item.toLowerCase()}`).classList.toggle("active", selected);
      $(`mode-${item.toLowerCase()}`).setAttribute("aria-pressed", String(selected));
    }
    resetSearchResults();
  }

  async function searchJourneys(event) {
    event.preventDefault();
    if (!state.catalog || state.busy) return;
    clearErrors("search-form", "search-error");
    resetSearchResults();
    const source = $("source").value;
    const destination = $("destination").value;
    const date = $("travel-date").value;
    const fields = {};
    if (!source) fields.source = "Choose a departure city.";
    if (!destination) fields.destination = "Choose a destination city.";
    else if (source === destination) fields.destination = "Choose a different destination.";
    if (!date) fields["travel-date"] = "Choose a departure date.";
    else if (date < state.catalog.today || date > state.catalog.max_date) fields["travel-date"] = `Choose a date from ${dateLabel(state.catalog.today, true)} to ${dateLabel(state.catalog.max_date, true)}.`;
    if (validationError("search-error", fields)) return;
    $("search-results").hidden = false;
    $("results-title").textContent = "Finding your journey…";
    $("results-count").textContent = "";
    $("results-date").textContent = "";
    $("service-list").replaceChildren(loading("Checking sample services…"));
    formBusy($("search-form"), true);
    try {
      const result = await latest("search", queryPath("/api/search", { source, destination, date, mode: state.mode }));
      $("results-title").textContent = `${result.source} to ${result.destination}`;
      $("results-date").textContent = `${dateLabel(result.date)} · ${result.mode} · Direct services`;
      $("results-count").textContent = `${result.results.length} ${result.results.length === 1 ? "journey" : "journeys"} found`;
      $("service-list").replaceChildren();
      if (!result.results.length) $("service-list").append(emptyState("A different route, perhaps?", "There are no direct sample services for this route and transport type. Try swapping the cities or switching between bus and train."));
      for (const item of result.results) {
        const row = node("article", "service-row");
        const fare = node("div", "fare-block");
        fare.append(node("strong", "", money(item.service.fare)), node("span", item.available ? "" : "full-service", item.available ? `${item.available} seats available` : `Full · ${item.waiting} waiting`));
        row.append(serviceName(item.service), serviceTimes(item.service), fare,
          action(item.available ? "Select journey" : "Join waiting list", "button secondary", () => chooseJourney(item.service.id, result.date, false), "arrow"));
        $("service-list").append(row);
      }
      $("search-results").scrollIntoView({ behavior: "smooth", block: "start" });
    } catch (error) {
      if (error.name !== "AbortError") {
        $("search-results").hidden = true;
        $("service-list").replaceChildren();
        fail("search-error", error, { source: "source", destination: "destination", date: "travel-date" });
      }
    } finally { if (!state.busy) formBusy($("search-form"), false); }
  }

  async function refreshJourney() {
    if (!state.selected) return null;
    const result = await latest("journey", queryPath("/api/journey", state.selected));
    state.journey = result;
    return result;
  }

  function maxPassengers() {
    return Math.min(6, Math.max(1, Number(state.catalog?.max_passengers_per_booking) || 6));
  }

  function activePassenger() {
    return state.passengers.find((passenger) => passenger.uid === state.activePassenger) || state.passengers[0];
  }

  function travellerLabel(passenger) {
    const index = state.passengers.indexOf(passenger) + 1;
    return `Traveller ${index}${passenger.name.trim() ? ` · ${passenger.name.trim()}` : ""}`;
  }

  function passengerField(passenger, key, label, options = {}) {
    const field = node("div", `field${options.full ? " full" : ""}`);
    const id = `traveller-${passenger.uid}-${key}`;
    const labelNode = node("label", "", label);
    labelNode.htmlFor = id;
    const control = node(key === "gender" ? "select" : "input");
    control.id = id;
    control.name = `travellers[${passenger.uid}][${key}]`;
    if (key === "gender") {
      for (const value of ["", "Female", "Male", "Other", "Prefer not to say"]) {
        const option = node("option", "", value || "Select gender");
        option.value = value;
        control.append(option);
      }
    } else if (key === "age") {
      control.type = "number";
      control.min = "1";
      control.max = "120";
      control.inputMode = "numeric";
      control.placeholder = "Years";
    } else {
      control.type = "text";
      control.maxLength = 100;
      control.autocomplete = "off";
      control.placeholder = "As you’d like it on the ticket";
    }
    control.required = true;
    control.value = passenger[key];
    control.setAttribute("aria-describedby", `${id}-error`);
    control.addEventListener(key === "gender" ? "change" : "input", () => {
      passenger[key] = control.value;
      if (key === "name") updateTravellerOptions();
    });
    const error = node("small", "field-error");
    error.id = `${id}-error`;
    field.append(labelNode, control, error);
    return field;
  }

  function renderPassengers(focusUid) {
    const rows = $("passenger-rows");
    rows.replaceChildren();
    for (const [index, passenger] of state.passengers.entries()) {
      const row = node("fieldset", "traveller-row");
      row.id = `traveller-${passenger.uid}`;
      row.append(node("legend", "", `Traveller ${index + 1}`));
      const tools = node("div", "traveller-tools");
      const remove = action(`Remove traveller ${index + 1}`, "text-button danger", () => removeTraveller(passenger.uid));
      remove.disabled = state.passengers.length === 1;
      tools.append(remove);
      const fields = node("div", "passenger-fields");
      fields.append(passengerField(passenger, "name", "Full name", { full: true }), passengerField(passenger, "age", "Age"), passengerField(passenger, "gender", "Gender"));
      const preference = node("div", "traveller-seat-summary");
      const summary = node("span");
      summary.id = `traveller-${passenger.uid}-preference`;
      const choose = action("Choose seat", "text-button", () => selectActiveTraveller(passenger.uid, true));
      choose.id = `traveller-${passenger.uid}-seat_number`;
      choose.setAttribute("aria-label", `Choose seat for traveller ${index + 1}`);
      choose.setAttribute("aria-describedby", `${choose.id}-error`);
      preference.append(summary, choose);
      const error = node("small", "field-error");
      error.id = `${choose.id}-error`;
      row.append(tools, fields, preference, error);
      rows.append(row);
    }
    if (!state.passengers.some((passenger) => passenger.uid === state.activePassenger)) state.activePassenger = state.passengers[0].uid;
    $("add-traveller").disabled = state.passengers.length >= maxPassengers();
    $("traveller-count").textContent = `${state.passengers.length} of ${maxPassengers()} travellers${state.passengers.length === maxPassengers() ? " · Maximum reached" : ""}`;
    $("traveller-limit-hint").textContent = `Add 1–${maxPassengers()} travellers. Each person receives their own ticket reference.`;
    updateTravellerOptions();
    updatePartySummary();
    if (focusUid) $(`traveller-${focusUid}-name`)?.focus();
  }

  function updateTravellerOptions() {
    const select = $("seat-traveller");
    select.replaceChildren();
    for (const passenger of state.passengers) {
      const option = node("option", "", travellerLabel(passenger));
      option.value = passenger.uid;
      select.append(option);
    }
    select.value = String(state.activePassenger);
    updatePreferenceLabels();
    const active = activePassenger();
    $("active-seat-note").textContent = `Editing ${travellerLabel(active)}. Other travellers’ selected seats are marked “Your party”.`;
    if (state.journey) renderSeats();
  }

  function updatePreferenceLabels() {
    for (const passenger of state.passengers) {
      const summary = $(`traveller-${passenger.uid}-preference`);
      if (summary) summary.textContent = passenger.seat_number === null ? "Seat: automatic allocation" : `Preferred seat: ${passenger.seat_number}`;
      const button = $(`traveller-${passenger.uid}-seat_number`);
      if (button) button.textContent = passenger.uid === state.activePassenger ? "Editing seat" : "Choose seat";
      $(`traveller-${passenger.uid}`)?.classList.toggle("active-traveller", passenger.uid === state.activePassenger);
    }
  }

  function addTraveller() {
    if (state.busy || state.passengers.length >= maxPassengers()) return;
    const passenger = { uid: state.nextPassengerId++, name: "", age: "", gender: "", seat_number: null };
    state.passengers.push(passenger);
    state.activePassenger = passenger.uid;
    clearErrors("passenger-form", "passenger-error");
    renderPassengers(passenger.uid);
  }

  function removeTraveller(uid) {
    if (state.busy || state.passengers.length === 1) return;
    const index = state.passengers.findIndex((passenger) => passenger.uid === uid);
    state.passengers.splice(index, 1);
    if (state.activePassenger === uid) state.activePassenger = state.passengers[Math.min(index, state.passengers.length - 1)].uid;
    clearErrors("passenger-form", "passenger-error");
    renderPassengers();
    $(`traveller-${state.activePassenger}-name`)?.focus();
  }

  function selectActiveTraveller(uid, focus = false) {
    if (state.busy) return;
    state.activePassenger = uid;
    $("seat-traveller").value = String(uid);
    updateTravellerOptions();
    if (focus) $("seat-traveller").focus();
  }

  function clearUnavailablePreferences(journey) {
    const lost = [];
    for (const passenger of state.passengers) {
      if (passenger.seat_number !== null && journey.seats.find((seat) => seat.number === passenger.seat_number)?.status !== "Available") {
        lost.push(travellerLabel(passenger));
        passenger.seat_number = null;
      }
    }
    return lost;
  }

  async function chooseJourney(serviceId, date, demo) {
    if (state.busy) return;
    const changed = !state.selected || state.selected.service_id !== serviceId || state.selected.date !== date || state.selected.demo !== demo;
    const hadPreferences = state.passengers.some((passenger) => passenger.seat_number !== null);
    if (changed) for (const passenger of state.passengers) passenger.seat_number = null;
    state.selected = { service_id: serviceId, date, demo };
    state.reviewing = null;
    state.journey = null;
    clearErrors("passenger-form", "passenger-error");
    show("details", demo ? "demo" : "home");
    renderPassengers();
    $("back-results").replaceChildren(icon("back"), document.createTextNode(demo ? "Back to demonstration" : "Back to journeys"));
    $("details-journey").replaceChildren();
    $("seat-map").replaceChildren();
    $("seat-status").textContent = changed && hadPreferences ? "Your traveller details are saved. Choose seat preferences for this new journey." : "";
    $("party-availability").textContent = "";
    $("seat-loading").hidden = false;
    $("details-fare").textContent = "";
    $("details-fare-breakdown").textContent = "";
    $("continue-review").disabled = true;
    $("auto-seat").disabled = true;
    try {
      const journey = await refreshJourney();
      const lost = clearUnavailablePreferences(journey);
      journeyStrip("details-journey", journey);
      renderSeats();
      updatePartySummary();
      $("continue-review").disabled = false;
      $("auto-seat").disabled = false;
      if (lost.length) notice("passenger-error", `Some preferred seats are no longer available (${lost.join(", ")}). Those travellers now use automatic allocation; please check before continuing.`);
      else if (changed && hadPreferences) $("seat-status").textContent += " Your traveller details are saved; previous seat preferences were cleared for this new journey.";
    } catch (error) {
      if (error.name !== "AbortError") {
        state.journey = null;
        fail("passenger-error", error);
        offerSeatRetry();
      }
    } finally { $("seat-loading").hidden = true; }
  }

  function renderSeats() {
    const journey = state.journey;
    if (!journey) return;
    const passenger = activePassenger();
    const train = journey.service.mode === "Train";
    $("seat-layout-description").textContent = train ? "18 seats · 3 + 3 seating · Sample coach" : "12 seats · 2 + 2 seating · Sample bus";
    const cabin = node("div", `seat-cabin${train ? " train-cabin" : ""}`);
    const front = node("div", "cabin-front");
    front.append(node("span", "", train ? "Coach A · Front" : "Front of bus"));
    if (!train) front.append(node("span", "steering-wheel"));
    const grid = node("div", `seat-grid${train ? " train-seats" : ""}`);
    grid.setAttribute("role", "group");
    grid.setAttribute("aria-label", `${journey.service.mode} seats for ${travellerLabel(passenger)}`);
    const eachSide = train ? 3 : 2;
    journey.seats.forEach((seat, index) => {
      if (index % (eachSide * 2) === eachSide) grid.append(node("span", "seat-aisle"));
      const booked = seat.status === "Booked";
      const owner = state.passengers.find((person) => person.seat_number === seat.number && person.uid !== passenger.uid);
      const selected = seat.number === passenger.seat_number;
      const button = action(String(seat.number), `seat${booked ? " booked" : owner ? " claimed" : ""}${selected ? " selected" : ""}`, () => {
        if (state.busy || state.passengers.some((person) => person.uid !== passenger.uid && person.seat_number === seat.number)) return;
        passenger.seat_number = seat.number;
        renderSeats();
        updatePreferenceLabels();
        updatePartySummary();
        $("seat-map").querySelector(`[data-seat="${seat.number}"]`)?.focus({ preventScroll: true });
      });
      button.dataset.seat = seat.number;
      button.disabled = booked || Boolean(owner);
      button.setAttribute("aria-label", `Seat ${seat.number}, ${booked ? "booked" : owner ? `selected for ${travellerLabel(owner)}` : selected ? `selected for ${travellerLabel(passenger)}` : "available"}`);
      button.setAttribute("aria-pressed", String(selected));
      grid.append(button);
    });
    cabin.append(front, grid);
    $("seat-map").replaceChildren(cabin);
    const automatic = passenger.seat_number === null;
    $("auto-seat").classList.toggle("active", automatic);
    $("auto-seat").setAttribute("aria-pressed", String(automatic));
    $("auto-seat").setAttribute("aria-label", `Automatic seat allocation for ${travellerLabel(passenger)}`);
    $("seat-status").textContent = `${travellerLabel(passenger)}: ${automatic ? "automatic allocation" : `preferred seat ${passenger.seat_number}`}. Selections are checked again when you reserve.`;
    updatePreferenceLabels();
  }

  /* Mirrors the disclosed allocation order for a preview; C++ makes the final allocation. */
  function previewAllocations(journey, passengers) {
    const free = journey.seats.filter((seat) => seat.status === "Available").map((seat) => seat.number).sort((a, b) => a - b);
    const reserved = new Set(passengers.filter((passenger) => passenger.seat_number !== null).map((passenger) => passenger.seat_number));
    const automaticSeats = free.filter((seat) => !reserved.has(seat));
    let queuePosition = journey.counts.waiting;
    return passengers.map((passenger) => {
      const seat = passenger.seat_number !== null ? passenger.seat_number : (automaticSeats.shift() ?? null);
      return { uid: passenger.uid, status: seat !== null ? "Confirmed" : "Waiting", seat_number: seat, queue_position: seat === null ? ++queuePosition : null };
    });
  }

  function updatePartySummary() {
    if (!state.journey) return;
    const count = state.passengers.length;
    const fare = state.journey.service.fare;
    $("details-fare").textContent = money(fare * count);
    $("details-fare-breakdown").textContent = `${count} ${count === 1 ? "traveller" : "travellers"} × ${money(fare)} per person`;
    const available = state.journey.counts.available;
    const confirmed = Math.min(available, count);
    const waiting = count - confirmed;
    $("party-availability").classList.toggle("has-waiting", waiting > 0);
    $("party-availability").textContent = waiting
      ? `${available} ${available === 1 ? "seat is" : "seats are"} available for ${count} ${count === 1 ? "traveller" : "travellers"}. ${confirmed} ${confirmed === 1 ? "person is" : "people are"} expected to be confirmed and ${waiting} to join the waiting queue. Selected seats are reserved first; remaining places follow traveller order.`
      : `There are enough seats for all ${count} ${count === 1 ? "traveller" : "travellers"} right now. Selected seats are reserved first, then automatic seats use the lowest free numbers.`;
  }

  function readParty() {
    return {
      contact: $("passenger-contact").value.trim(),
      passengers: state.passengers.map((passenger) => ({ name: passenger.name.trim(), age: Number(passenger.age), gender: passenger.gender, seat_number: passenger.seat_number }))
    };
  }

  async function reviewReservation(event) {
    event.preventDefault();
    if (state.busy || !state.journey) return;
    clearErrors("passenger-form", "passenger-error");
    const party = readParty();
    const fields = {};
    if (!/^[+\d\s()-]+$/.test(party.contact) || !/^\d{10,15}$/.test(party.contact.replace(/[+\s()-]/g, ""))) fields["passenger-contact"] = "Contact number: enter 10–15 digits.";
    const preferred = new Set();
    const names = new Set();
    state.passengers.forEach((passenger, index) => {
      const prefix = `traveller-${passenger.uid}`;
      const member = party.passengers[index];
      if (!member.name) fields[`${prefix}-name`] = `Traveller ${index + 1}: enter a full name.`;
      else if (names.has(member.name.toLocaleLowerCase())) fields[`${prefix}-name`] = `Traveller ${index + 1}: each traveller’s name must be different for this shared contact.`;
      else names.add(member.name.toLocaleLowerCase());
      if (!passenger.age || !Number.isInteger(member.age) || member.age < 1 || member.age > 120) fields[`${prefix}-age`] = `Traveller ${index + 1}: enter a whole age from 1 to 120.`;
      if (!member.gender) fields[`${prefix}-gender`] = `Traveller ${index + 1}: select a gender option.`;
      if (member.seat_number !== null) {
        if (preferred.has(member.seat_number)) fields[`${prefix}-seat_number`] = `Traveller ${index + 1}: choose a different seat; this one is selected by another traveller.`;
        preferred.add(member.seat_number);
      }
    });
    if (validationError("passenger-error", fields)) return;
    state.reviewing = null;
    formBusy($("passenger-form"), true);
    state.journey = null;
    $("seat-map").replaceChildren();
    $("seat-loading").hidden = false;
    try {
      const journey = await refreshJourney();
      const lost = clearUnavailablePreferences(journey);
      renderSeats();
      updatePartySummary();
      if (lost.length) {
        throw new ApiError(`Preferred seats are no longer available for ${lost.join(", ")}. Their preferences have changed to automatic; please check the seat choices and review again.`);
      }
      if (JSON.stringify(readParty()) !== JSON.stringify(party)) throw new ApiError("Traveller details changed while we checked availability. Your changes are saved; please review the updated details again.");
      state.reviewing = party;
      renderReview();
      show("review", journey.demo ? "demo" : "home");
    } catch (error) {
      fail("passenger-error", error);
      if (error.name !== "AbortError" && !state.journey) offerSeatRetry();
    } finally {
      $("seat-loading").hidden = true;
      if (!state.busy) formBusy($("passenger-form"), false);
      if (!state.journey) $("continue-review").disabled = true;
    }
  }

  function renderReview() {
    const journey = state.journey;
    const party = state.reviewing;
    const allocations = previewAllocations(journey, state.passengers);
    const sheet = node("div", "review-sheet group-review");
    const travellers = node("section", "review-travellers");
    travellers.append(node("h2", "", `Your ${party.passengers.length === 1 ? "traveller" : `${party.passengers.length} travellers`}`));
    const contact = node("p", "review-contact", `Shared contact: ${party.contact}`);
    travellers.append(contact);
    party.passengers.forEach((passenger, index) => {
      const allocation = allocations[index];
      const row = node("div", "review-traveller-row");
      const title = node("div", "review-traveller-heading");
      title.append(node("h3", "", `${index + 1}. ${passenger.name}`), statusBadge(allocation.status));
      const details = node("dl");
      details.append(dataCell("Age / gender", `${passenger.age} / ${passenger.gender}`),
        dataCell("Preference", passenger.seat_number === null ? "Automatic" : `Seat ${passenger.seat_number}`),
        dataCell("Expected allocation", allocation.seat_number === null ? `Waiting · queue ${allocation.queue_position}` : `Seat ${allocation.seat_number}`),
        dataCell("Sample fare", money(journey.service.fare)));
      row.append(title, details);
      travellers.append(row);
    });
    const side = node("section", "review-side");
    side.append(node("h2", "", "The journey"));
    const service = node("div", "review-service");
    service.append(node("h3", "", `${journey.service.source} to ${journey.service.destination}`),
      node("p", "", `${journey.service.name} · ${journey.service.number} · ${journey.service.mode}`),
      node("p", "", `${dateLabel(journey.date, true)} · ${journey.service.departure} – ${journey.service.arrival}${journey.service.arrival_day_offset ? " (+1 day)" : ""}`));
    if (journey.demo) service.append(node("span", "tag", "Fictional demonstration"));
    const confirmed = allocations.filter((allocation) => allocation.status === "Confirmed").length;
    const waiting = allocations.length - confirmed;
    const warning = node("p", `review-allocation-note${waiting ? " has-waiting" : ""}`, waiting
      ? `${confirmed} expected confirmed · ${waiting} expected waiting. Travellers marked Waiting will not have a seat yet. They join after the existing queue and will receive seats in queue order when cancellations occur.`
      : `All ${allocations.length} ${allocations.length === 1 ? "traveller is" : "travellers are"} expected to receive a confirmed seat.`);
    const total = node("div", "fare-total");
    total.append(node("span", "", "Total sample fare"), node("strong", "", money(journey.service.fare * party.passengers.length)));
    side.append(service, warning, node("p", "muted small", "Selected seats are reserved first. Automatic allocation uses the lowest remaining seats, in traveller order. Final allocations can change until confirmation."), total,
      node("p", "muted small", `${party.passengers.length} × ${money(journey.service.fare)} · includes waiting travellers. No payment is collected.`));
    sheet.append(travellers, side);
    $("review-content").replaceChildren(sheet);
    $("confirm-booking").replaceChildren(document.createTextNode(party.passengers.length === 1 ? "Reserve my place" : `Reserve ${party.passengers.length} places`), icon("arrow"));
    notice("review-error", "");
  }

  function groupFieldMap() {
    const fields = { contact: "passenger-contact" };
    state.passengers.forEach((passenger, index) => {
      for (const key of ["name", "age", "gender", "seat_number"]) fields[`passengers.${index}.${key}`] = `traveller-${passenger.uid}-${key}`;
    });
    return fields;
  }

  async function confirmBooking() {
    if (state.busy || !state.reviewing || !state.journey) return;
    notice("review-error", "");
    const button = $("confirm-booking");
    const oldText = button.textContent;
    button.textContent = "Reserving your places…";
    try {
      const result = await mutate("/api/book/group", { ...state.selected, ...state.reviewing });
      state.journey = result.journey;
      state.receipt = { tickets: result.tickets, total_fare: result.total_fare, journey: result.journey };
      state.receiptVersion++;
      notice("group-message", "");
      renderGroupReceipt();
      show("group", result.journey.demo ? "demo" : "home");
      resetSearchResults();
    } catch (error) {
      const fieldMap = groupFieldMap();
      if (Object.keys(error.fields || {}).some((field) => fieldMap[field])) {
        show("details", state.selected.demo ? "demo" : "home");
        const mapped = {};
        for (const [key, message] of Object.entries(error.fields)) if (fieldMap[key]) mapped[fieldMap[key]] = message;
        validationError("passenger-error", mapped);
        if (error.status === 409) $("passenger-error").append(node("p", "", "No traveller in this request was booked. Update the affected preference and review again."));
      } else {
        fail("review-error", error);
        if (error.status === 409) {
          const edit = action("Refresh seats and edit reservation", "text-button", () => chooseJourney(state.selected.service_id, state.selected.date, state.selected.demo));
          $("review-error").append(node("br"), edit);
        }
      }
    } finally { button.replaceChildren(document.createTextNode(oldText), icon("arrow")); }
  }

  function renderGroupReceipt() {
    const receipt = state.receipt;
    if (!receipt || !receipt.tickets.length) return;
    $("print-group").disabled = false;
    $("refresh-group").disabled = false;
    const tickets = receipt.tickets;
    const first = tickets[0];
    const confirmed = tickets.filter((ticket) => ticket.status === "Confirmed").length;
    const waiting = tickets.filter((ticket) => ticket.status === "Waiting").length;
    const cancelled = tickets.length - confirmed - waiting;
    $("group-eyebrow").textContent = first.demo ? "Fictional demonstration reservation" : "Good company, one journey";
    setHeading("group-title", "Your journey, ", "reserved.");
    $("group-description").textContent = `${confirmed} confirmed · ${waiting} waiting${cancelled ? ` · ${cancelled} cancelled` : ""}. Every traveller has their own reference and uses the same contact number.`;
    const content = $("group-content");
    content.replaceChildren();
    const header = node("div", "group-receipt-header");
    const route = node("div");
    route.append(node("h2", "", `${first.service.source} to ${first.service.destination}`), node("p", "", `${first.service.name} · ${first.service.number} · ${first.service.mode}`),
      node("p", "", `${dateLabel(first.date)} · ${first.service.departure} – ${first.service.arrival}${first.service.arrival_day_offset ? " (+1 day)" : ""}`));
    const contact = node("div", "group-receipt-contact");
    contact.append(node("span", "", "Shared contact number"), node("strong", "", first.contact));
    header.append(route, contact);
    content.append(header);
    if (waiting) content.append(node("p", "group-waiting-warning", `${waiting} ${waiting === 1 ? "traveller has" : "travellers have"} a waiting ticket and no seat yet. Queue positions below are current when retrieved. Refresh status or look up each reference to check for promotion.`));
    for (const [index, ticket] of tickets.entries()) {
      const row = node("article", "group-ticket-row");
      const identity = node("div", "group-ticket-person");
      identity.append(node("p", "ticket-person-number", `Traveller ${index + 1} · ${ticket.passenger_id}`), node("h3", "", ticket.name), node("p", "muted small", `${ticket.age} years · ${ticket.gender}`));
      const allocation = node("div", "group-ticket-allocation");
      allocation.append(statusBadge(ticket.status), node("strong", "", ticket.status === "Confirmed" ? `Seat ${ticket.seat_number}` : ticket.status === "Waiting" ? `Queue ${ticket.queue_position}` : "Cancelled"), node("span", "muted small", money(ticket.fare)));
      const reference = node("div", "group-ticket-reference");
      reference.append(node("span", "", "Booking reference"), node("strong", "", ticket.reference));
      const actions = node("div", "group-ticket-actions");
      actions.append(action("View ticket", "text-button", () => { renderTicket(ticket); show("ticket", ticket.demo ? "demo" : "home"); }));
      if (ticket.status !== "Cancelled") actions.append(action("Cancel traveller", "text-button danger", () => cancelGroupTicket(ticket)));
      row.append(identity, allocation, reference, actions);
      content.append(row);
    }
    const total = node("div", "group-receipt-total");
    total.append(node("p", "", `${tickets.length} ${tickets.length === 1 ? "traveller" : "travellers"} · ${cancelled ? "Original sample total" : "Total sample fare"} · no payment collected`), node("strong", "", money(receipt.total_fare)));
    content.append(total, node("p", "print-note", "College project • Sample schedules and fares. Each traveller’s booking reference and the shared contact number are needed to find or cancel that ticket."));
    notice("group-error", "");
  }

  async function refreshGroupReceipt(message = "") {
    if (state.busy || !state.receipt) return;
    const receipt = state.receipt;
    const version = ++state.receiptVersion;
    $("group-description").textContent = "Checking current ticket status for every traveller…";
    $("group-content").replaceChildren(loading("Refreshing every traveller’s ticket…"));
    $("refresh-group").disabled = true;
    $("print-group").disabled = true;
    notice("group-error", "");
    notice("group-message", "");
    try {
      const results = await Promise.all(receipt.tickets.map((ticket, index) => latest(`receipt-${index}`, "/api/ticket", postOptions({ reference: ticket.reference, contact: ticket.contact }))));
      if (state.receipt !== receipt || state.receiptVersion !== version || state.view !== "group") return;
      receipt.tickets = results.map((result) => result.ticket);
      renderGroupReceipt();
      if (message) notice("group-message", message);
    } catch (error) {
      if (error.name !== "AbortError" && state.receipt === receipt && state.receiptVersion === version && state.view === "group") {
        cancelReads("catalog");
        $("group-content").replaceChildren();
        $("group-description").textContent = "Ticket status could not be refreshed. Try again to see the current allocations.";
        fail("group-error", error);
        $("group-error").append(node("br"), action("Try refreshing tickets again", "text-button", () => refreshGroupReceipt(message)));
      }
    } finally {
      if (state.receiptVersion === version) {
        $("refresh-group").disabled = false;
        $("print-group").disabled = !$("group-content").querySelector(".group-ticket-row");
      }
    }
  }

  function updateReceiptTicket(ticket) {
    if (!state.receipt) return;
    state.receipt.tickets = state.receipt.tickets.map((member) => member.reference === ticket.reference ? ticket : member);
  }

  async function cancelGroupTicket(ticket) {
    if (state.busy || ticket.status === "Cancelled") return;
    const description = ticket.status === "Waiting" ? "This traveller will leave the waiting queue. The other travellers keep their reservations." : `Only ${ticket.name}’s reservation will be cancelled. Seat ${ticket.seat_number} goes to the first waiting traveller if there is one. The other reservations remain active.`;
    if (!await askConfirmation(`Cancel ${ticket.name}’s ticket?`, description, "Cancel traveller")) return;
    notice("group-error", "");
    try {
      const result = await mutate("/api/cancel", { reference: ticket.reference, contact: ticket.contact });
      updateReceiptTicket(result.cancelled);
      await refreshGroupReceipt(cancellationMessage(result, ticket.demo));
    } catch (error) { fail("group-error", error); }
  }

  function renderTicket(ticket) {
    state.ticket = ticket;
    $("ticket-back-group").hidden = !state.receipt?.tickets.some((member) => member.reference === ticket.reference);
    notice("ticket-error", "");
    notice("ticket-message", "");
    const confirmed = ticket.status === "Confirmed";
    const waiting = ticket.status === "Waiting";
    $("ticket-symbol").className = `success-symbol${waiting ? " waiting" : confirmed ? "" : " cancelled"}`;
    $("ticket-symbol").replaceChildren(icon(waiting ? "clock" : "check"));
    $("ticket-eyebrow").textContent = ticket.demo ? "Fictional demonstration ticket" : confirmed ? "Ready for the road" : waiting ? "Your place in line is saved" : "Reservation history";
    setHeading("ticket-title", confirmed ? "You’re " : waiting ? "You’re on " : "Reservation ", confirmed ? "going places." : waiting ? "the list." : "cancelled.");
    $("ticket-description").textContent = confirmed
      ? "Your reservation is saved. Keep your reference and contact number handy."
      : waiting ? `You’re number ${ticket.queue_position} in the waiting queue. Look up this ticket to check for a confirmed seat.`
        : "This reservation has been cancelled. Your reference remains available for your records.";
    const top = node("div", "ticket-top");
    const wordmark = node("div", "ticket-wordmark", "journeydesk.");
    wordmark.append(node("small", "", ticket.demo ? "Fictional sample reservation" : "Your journey, reserved"));
    top.append(wordmark, statusBadge(ticket.status));
    const route = node("div", "ticket-route");
    const from = node("div");
    from.append(node("h2", "", ticket.service.source), node("p", "", `Departure ${ticket.service.departure}`));
    const to = node("div", "destination");
    to.append(node("h2", "", ticket.service.destination), node("p", "", `Arrival ${ticket.service.arrival}${ticket.service.arrival_day_offset ? " (+1 day)" : ""}`));
    route.append(from, icon("arrow"), to);
    const body = node("dl", "ticket-body");
    body.append(dataCell("Traveller", ticket.name), dataCell("Passenger ID", ticket.passenger_id),
      dataCell(confirmed ? "Your seat" : waiting ? "Queue position" : "Allocation", confirmed ? `Seat ${ticket.seat_number}` : waiting ? `Waiting · ${ticket.queue_position}` : "Cancelled"),
      dataCell("Journey date", dateLabel(ticket.date, true)), dataCell("Service", `${ticket.service.number} · ${ticket.service.mode}`),
      dataCell("Contact number", ticket.contact), dataCell("Age / gender", `${ticket.age} / ${ticket.gender}`),
      dataCell("Service name", ticket.service.name), dataCell("Duration", duration(ticket.service.duration_minutes)));
    const bottom = node("div", "ticket-bottom");
    const reference = node("div");
    reference.append(node("p", "", "Booking reference"), node("span", "reference", ticket.reference));
    const fare = node("div");
    fare.append(node("p", "", "Sample fare · no payment collected"), node("strong", "", money(ticket.fare)));
    bottom.append(reference, fare);
    $("ticket-content").replaceChildren(top, route, body, bottom, node("p", "print-note", "College project • Sample schedules and fares. Keep this booking reference and the contact number to retrieve your reservation."));
    $("cancel-ticket").hidden = ticket.status === "Cancelled";
    $("print-ticket").replaceChildren(icon("print"), document.createTextNode(ticket.status === "Cancelled" ? "Print record" : "Print ticket"));
  }

  async function lookupTicket(event) {
    event.preventDefault();
    if (state.busy) return;
    clearErrors("lookup-form", "lookup-error");
    state.ticket = null;
    $("ticket-content").replaceChildren();
    const reference = $("lookup-reference").value.trim();
    const contact = $("lookup-contact").value.trim();
    const fields = {};
    if (!reference) fields["lookup-reference"] = "Enter your booking reference.";
    if (!contact) fields["lookup-contact"] = "Enter the contact number used for this booking.";
    if (validationError("lookup-error", fields)) return;
    formBusy($("lookup-form"), true);
    try {
      const result = await latest("ticket", "/api/ticket", postOptions({ reference, contact }));
      renderTicket(result.ticket);
      show("ticket", "lookup");
    } catch (error) { fail("lookup-error", error, { reference: "lookup-reference", contact: "lookup-contact" }); }
    finally { if (!state.busy) formBusy($("lookup-form"), false); }
  }

  function askConfirmation(title, description, confirmLabel, backLabel = "Keep reservation") {
    return new Promise((resolve) => {
      const dialog = $("confirm-dialog");
      $("dialog-title").textContent = title;
      $("dialog-description").textContent = description;
      $("dialog-confirm").textContent = confirmLabel;
      $("dialog-back").textContent = backLabel;
      let answered = false;
      function finish(confirmed) {
        if (answered) return;
        answered = true;
        dialog.close();
        $("dialog-confirm").removeEventListener("click", accept);
        $("dialog-back").removeEventListener("click", decline);
        dialog.removeEventListener("cancel", cancel);
        resolve(confirmed);
      }
      const accept = () => finish(true);
      const decline = () => finish(false);
      const cancel = (event) => { event.preventDefault(); finish(false); };
      $("dialog-confirm").addEventListener("click", accept);
      $("dialog-back").addEventListener("click", decline);
      dialog.addEventListener("cancel", cancel);
      dialog.showModal();
      $("dialog-back").focus();
    });
  }

  function cancellationMessage(result, demo) {
    if (!result.promoted) return "Reservation cancelled. The journey and waiting queue are up to date.";
    return demo
      ? `Reservation cancelled. ${result.promoted.passenger_id} ${result.promoted.name}, first in the queue, now holds seat ${result.promoted.seat_number} — the exact seat released.`
      : `Reservation cancelled. Seat ${result.promoted.seat_number} has been assigned to the next traveller in the waiting queue.`;
  }

  async function cancelTicket() {
    if (state.busy || !state.ticket || state.ticket.status === "Cancelled") return;
    const ticket = state.ticket;
    const description = ticket.status === "Waiting"
      ? "You will leave the waiting queue. This action does not release a booked seat."
      : `Your seat ${ticket.seat_number} will be released. If someone is waiting, they will receive this exact seat. This cannot be undone.`;
    if (!await askConfirmation(`Cancel ${ticket.reference}?`, description, "Cancel reservation")) return;
    notice("ticket-error", "");
    try {
      const result = await mutate("/api/cancel", { reference: ticket.reference, contact: ticket.contact });
      updateReceiptTicket(result.cancelled);
      renderTicket(result.cancelled);
      notice("ticket-message", cancellationMessage(result, ticket.demo));
    } catch (error) { fail("ticket-error", error); }
  }

  function fillServiceOptions(demo = false) {
    const select = $("desk-service");
    const current = select.value;
    select.replaceChildren();
    if (!state.catalog) return;
    for (const service of state.catalog.services) {
      if (demo && service.id !== state.catalog.demo_service) continue;
      const option = node("option", "", `${service.number} · ${service.source} → ${service.destination} · ${service.mode} · ${service.departure}`);
      option.value = service.id;
      select.append(option);
    }
    if (!demo && [...select.options].some((option) => option.value === current)) select.value = current;
    select.disabled = demo;
  }

  function clearDesk() {
    state.reads.get("desk")?.controller.abort();
    state.reads.delete("desk");
    state.deskSnapshot = null;
    $("desk-content").hidden = true;
    $("desk-summary").replaceChildren();
    $("manifest-passengers").replaceChildren();
    $("manifest-waiting").replaceChildren();
    formBusy($("desk-form"), false);
  }

  function openDesk(demo, focus = true) {
    if (state.busy) return;
    state.demo = demo;
    clearDesk();
    clearErrors("desk-form", "desk-error");
    notice("desk-notice", "");
    show("desk", demo ? "demo" : "desk", focus);
    $("desk-view").classList.toggle("demo-mode", demo);
    $("demo-intro").hidden = !demo;
    $("desk-form").hidden = demo && !state.demoReady;
    $("reset-demo").hidden = !demo || !state.demoReady;
    $("load-demo").textContent = state.demoReady ? "Open fictional demo" : "Load fictional demo";
    $("load-demo").append(icon("arrow"));
    $("desk-eyebrow").textContent = demo ? "A closer look at the assignment" : "Behind the journey";
    setHeading("desk-title", demo ? "See the queue " : "The reservation ", demo ? "in motion." : "desk.");
    $("desk-description").textContent = demo ? "An isolated fictional bus journey, built to show fair seat allocation." : "View passenger records and the waiting queue for a service.";
    fillServiceOptions(demo);
    $("desk-date").value = state.catalog ? (demo ? state.catalog.demo_date : state.catalog.default_date) : "";
    $("desk-date").disabled = demo;
    $("desk-date").removeAttribute("min");
    $("desk-date").removeAttribute("max");
    $("desk-key-field").hidden = demo;
    $("desk-query").value = "";
    $("desk-key").value = state.deskKey;
    if (demo && state.demoReady) loadManifest();
  }

  async function loadDemo(reset = false) {
    if (state.busy || !state.catalog) return;
    if (reset && !await askConfirmation("Reset the fictional demonstration?", "The demo will return to 12 booked seats and the original P108, P109 and P110 waiting queue. Changes to this demo will be removed. Regular reservations are separate.", "Reset demo", "Keep current demo")) return;
    clearDesk();
    notice("desk-error", "");
    notice("desk-notice", "");
    try {
      const result = await mutate("/api/demo", { reset });
      state.demoReady = true;
      $("desk-form").hidden = false;
      $("reset-demo").hidden = false;
      $("load-demo").replaceChildren(document.createTextNode("Open fictional demo"), icon("arrow"));
      $("desk-service").value = result.service_id;
      $("desk-date").value = result.date;
      $("desk-query").value = "";
      await loadManifest();
      if (state.view === "desk" && state.demo) notice("desk-notice", reset ? "The fictional demonstration has been reset to its original sample records." : "Fictional demonstration loaded. Cancel a confirmed passenger to watch the first waiting passenger receive that exact seat.");
    } catch (error) { fail("desk-error", error); }
  }

  async function loadManifest(event) {
    event?.preventDefault();
    if (state.busy || !state.catalog) return;
    clearErrors("desk-form", "desk-error");
    notice("desk-notice", "");
    clearDesk();
    const snapshot = {
      service_id: $("desk-service").value,
      date: $("desk-date").value,
      demo: state.demo,
      q: $("desk-query").value.trim()
    };
    if (!state.demo) state.deskKey = $("desk-key").value.trim();
    const fields = {};
    if (!snapshot.service_id) fields["desk-service"] = "Choose a service.";
    if (!snapshot.date) fields["desk-date"] = "Choose a journey date.";
    if (!snapshot.demo && !state.deskKey) fields["desk-key"] = "Enter the desk access key printed by the server.";
    if (validationError("desk-error", fields)) return;
    formBusy($("desk-form"), true);
    $("desk-content").hidden = false;
    $("desk-summary").replaceChildren(loading("Opening the passenger manifest…"));
    try {
      const result = await latest("desk", queryPath("/api/desk", snapshot), { headers: snapshot.demo ? {} : { "X-Desk-Key": state.deskKey } });
      state.deskSnapshot = snapshot;
      renderManifest(result);
    } catch (error) {
      if (error.name !== "AbortError") {
        clearDesk();
        fail("desk-error", error, { service_id: "desk-service", date: "desk-date" });
        if (error.status === 401) fieldError("desk-key", "This access key was not accepted. Check the local server terminal.");
      }
    } finally { if (!state.busy) formBusy($("desk-form"), false); }
  }

  function renderManifest(result) {
    const journey = result.journey;
    const heading = node("div");
    heading.append(node("h2", "", `${journey.service.source} to ${journey.service.destination}`), node("p", "", `${journey.service.name} · ${journey.service.number} · ${dateLabel(journey.date, true)}${journey.demo ? " · Fictional demo" : ""}`));
    const counts = node("div", "count-line");
    for (const [label, value] of [["Available", journey.counts.available], ["Confirmed", journey.counts.booked], ["Waiting", journey.counts.waiting]]) {
      const count = node("div", "muted");
      count.append(node("strong", "", value), document.createTextNode(label));
      counts.append(count);
    }
    $("desk-summary").replaceChildren(heading, counts);
    if (journey.demo) {
      const tryBooking = action("Try a demo reservation", "text-button", () => chooseJourney(journey.service.id, journey.date, true), "arrow");
      heading.append(tryBooking);
    }
    const filtered = Boolean(state.deskSnapshot.q);
    $("passenger-list-count").textContent = `${result.passengers.length}${filtered ? " matching" : " active"} ${result.passengers.length === 1 ? "passenger" : "passengers"}`;
    $("waiting-list-count").textContent = `${result.waiting.length}${filtered ? " matching" : ""} ${result.waiting.length === 1 ? "traveller" : "travellers"}`;
    manifestTable("manifest-passengers", result.passengers, false, filtered);
    manifestTable("manifest-waiting", result.waiting, true, filtered);
  }

  function manifestTable(containerId, passengers, waiting, filtered) {
    const container = $(containerId);
    container.replaceChildren();
    if (!passengers.length) {
      container.append(emptyState(filtered ? "No matching passengers" : waiting ? "No one is waiting." : "A fresh start for this journey.", filtered ? "Try another name, passenger ID or reference, or clear the filter." : waiting ? "When all seats are booked, new reservations join this queue." : "No confirmed passenger records have been added to this service and date."));
      return;
    }
    const scroll = node("div", "table-scroll");
    scroll.tabIndex = 0;
    scroll.setAttribute("role", "region");
    scroll.setAttribute("aria-label", `${waiting ? "Waiting queue" : "Passenger manifest"}; scroll horizontally on narrow screens`);
    const table = node("table", "manifest-table");
    const caption = node("caption", "sr-only", waiting ? "Passengers in actual waiting queue order" : "Confirmed passengers, sorted by seat number");
    const head = node("thead");
    const headRow = node("tr");
    for (const title of [waiting ? "Queue" : "Seat", "Passenger", "Contact", "Age / gender", "Action"]) {
      const th = node("th", "", title);
      th.scope = "col";
      headRow.append(th);
    }
    head.append(headRow);
    const body = node("tbody");
    for (const passenger of passengers) {
      const row = node("tr");
      const position = node("td", "queue-cell", waiting ? passenger.queue_position : passenger.seat_number);
      const name = node("td", "passenger-cell", `${passenger.passenger_id} · ${passenger.name}`);
      name.append(node("small", "", passenger.reference));
      const cancel = node("td");
      cancel.append(action("Cancel", "text-button danger", () => cancelFromDesk(passenger)));
      row.append(position, name, node("td", "", passenger.contact), node("td", "", `${passenger.age} / ${passenger.gender}`), cancel);
      body.append(row);
    }
    table.append(caption, head, body);
    scroll.append(table);
    container.append(scroll);
  }

  async function cancelFromDesk(passenger) {
    if (state.busy || !state.deskSnapshot) return;
    const snapshot = { ...state.deskSnapshot };
    const message = passenger.status === "Waiting" ? "This traveller will leave the waiting queue. No booked seat will be released." : `Seat ${passenger.seat_number} will be released to the first waiting traveller, if there is one. This cannot be undone.`;
    if (!await askConfirmation(`Cancel ${passenger.passenger_id} · ${passenger.name}?`, message, "Cancel reservation")) return;
    notice("desk-error", "");
    notice("desk-notice", "");
    try {
      const result = await mutate("/api/desk/cancel", { reference: passenger.reference, service_id: snapshot.service_id, date: snapshot.date, demo: snapshot.demo }, snapshot.demo ? {} : { "X-Desk-Key": state.deskKey });
      await loadManifest();
      if (state.view === "desk" && state.demo === snapshot.demo) notice("desk-notice", cancellationMessage(result, snapshot.demo));
    } catch (error) { clearDesk(); fail("desk-error", error); }
  }

  function navigate(destination, focus = true) {
    if (state.busy) return;
    if (destination === "desk" || destination === "demo") { openDesk(destination === "demo", focus); return; }
    if (destination === "lookup") {
      clearErrors("lookup-form", "lookup-error");
      show("lookup", "lookup", focus);
    } else {
      show("home", "home", focus);
    }
  }

  async function initialize() {
    $("search-button").disabled = true;
    $("desk-submit").disabled = true;
    $("load-demo").disabled = true;
    try {
      const catalog = await latest("catalog", "/api/catalog");
      state.catalog = catalog;
      for (const id of ["source", "destination"]) {
        $(id).replaceChildren();
        const placeholder = node("option", "", id === "source" ? "Departure city" : "Destination city");
        placeholder.value = "";
        $(id).append(placeholder);
        for (const city of catalog.cities) {
          const option = node("option", "", city);
          option.value = city;
          $(id).append(option);
        }
      }
      $("source").value = catalog.cities.includes("Ahmedabad") ? "Ahmedabad" : "";
      $("destination").value = catalog.cities.includes("Mumbai") ? "Mumbai" : "";
      $("travel-date").min = catalog.today;
      $("travel-date").max = catalog.max_date;
      $("travel-date").value = catalog.default_date;
      $("desk-date").value = catalog.default_date;
      fillServiceOptions();
      $("search-button").disabled = false;
      $("desk-submit").disabled = false;
      $("load-demo").disabled = false;
      notice("app-notice", "");
      const destinations = { "#ticket": "lookup", "#desk": "desk", "#demo": "demo", "#book": "home" };
      navigate(destinations[location.hash] || "home", false);
    } catch (error) {
      if (error.name === "AbortError") return;
      notice("app-notice", error.message);
      $("app-notice").append(document.createTextNode(" "), action("Try again", "text-button", initialize));
      for (const id of ["source", "destination"]) {
        const option = node("option", "", "Cities unavailable");
        option.value = "";
        $(id).replaceChildren(option);
      }
    }
  }

  for (const link of document.querySelectorAll("[data-nav]")) {
    link.addEventListener("click", (event) => {
      event.preventDefault();
      if (!state.busy) {
        history.replaceState(null, "", link.getAttribute("href"));
        navigate(link.dataset.nav);
      }
    });
  }
  $("mode-bus").addEventListener("click", () => changeMode("Bus"));
  $("mode-train").addEventListener("click", () => changeMode("Train"));
  $("swap").addEventListener("click", () => {
    const source = $("source").value;
    $("source").value = $("destination").value;
    $("destination").value = source;
    resetSearchResults();
  });
  for (const id of ["source", "destination", "travel-date"]) $(id).addEventListener("change", resetSearchResults);
  $("search-form").addEventListener("submit", searchJourneys);
  $("back-results").addEventListener("click", () => {
    if (state.selected?.demo) openDesk(true);
    else show("home", "home");
  });
  $("add-traveller").addEventListener("click", addTraveller);
  $("seat-traveller").addEventListener("change", () => selectActiveTraveller(Number($("seat-traveller").value)));
  $("auto-seat").addEventListener("click", () => { activePassenger().seat_number = null; renderSeats(); updatePartySummary(); });
  $("passenger-form").addEventListener("submit", reviewReservation);
  $("back-details").addEventListener("click", () => { show("details", state.selected.demo ? "demo" : "home"); renderSeats(); });
  $("confirm-booking").addEventListener("click", confirmBooking);
  $("print-ticket").addEventListener("click", () => window.print());
  $("cancel-ticket").addEventListener("click", cancelTicket);
  function startAnotherJourney() {
    $("passenger-form").reset();
    state.passengers = [{ uid: state.nextPassengerId++, name: "", age: "", gender: "", seat_number: null }];
    state.activePassenger = state.passengers[0].uid;
    state.reviewing = null;
    state.receipt = null;
    state.receiptVersion++;
    renderPassengers();
    clearErrors("passenger-form", "passenger-error");
    navigate("home");
  }
  $("ticket-new").addEventListener("click", startAnotherJourney);
  $("group-new").addEventListener("click", startAnotherJourney);
  $("print-group").addEventListener("click", () => window.print());
  $("refresh-group").addEventListener("click", () => refreshGroupReceipt());
  $("ticket-back-group").addEventListener("click", () => {
    if (!state.receipt) return;
    show("group", state.receipt.tickets[0].demo ? "demo" : "home");
    refreshGroupReceipt();
  });
  $("lookup-form").addEventListener("submit", lookupTicket);
  $("desk-form").addEventListener("submit", loadManifest);
  $("load-demo").addEventListener("click", () => loadDemo(false));
  $("reset-demo").addEventListener("click", () => loadDemo(true));
  for (const id of ["desk-service", "desk-date", "desk-query", "desk-key"]) $(id).addEventListener("input", () => {
    clearDesk();
    notice("desk-notice", "");
  });
  window.addEventListener("hashchange", () => {
    if (location.hash === "#main") return;
    const destinations = { "#ticket": "lookup", "#desk": "desk", "#demo": "demo" };
    navigate(destinations[location.hash] || "home");
  });
  renderPassengers();
  initialize();
})();
