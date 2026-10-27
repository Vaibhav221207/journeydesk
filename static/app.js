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
    preferredSeat: null,
    passenger: null,
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
    notice(summaryId, "Please check the highlighted details and try again.");
    $(Object.keys(fields)[0]).focus();
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

  async function chooseJourney(serviceId, date, demo) {
    if (state.busy) return;
    state.selected = { service_id: serviceId, date, demo };
    state.preferredSeat = null;
    state.journey = null;
    clearErrors("passenger-form", "passenger-error");
    show("details", demo ? "demo" : "home");
    $("back-results").replaceChildren(icon("back"), document.createTextNode(demo ? "Back to demonstration" : "Back to journeys"));
    $("details-journey").replaceChildren();
    $("seat-map").replaceChildren();
    $("seat-status").textContent = "";
    $("seat-loading").hidden = false;
    $("details-fare").textContent = "";
    $("continue-review").disabled = true;
    $("auto-seat").disabled = true;
    try {
      const journey = await refreshJourney();
      journeyStrip("details-journey", journey);
      $("details-fare").textContent = money(journey.service.fare);
      renderSeats();
      $("continue-review").disabled = false;
      $("auto-seat").disabled = false;
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
    const train = journey.service.mode === "Train";
    $("seat-layout-description").textContent = train ? "18 seats · 3 + 3 seating · Sample coach" : "12 seats · 2 + 2 seating · Sample bus";
    const cabin = node("div", `seat-cabin${train ? " train-cabin" : ""}`);
    const front = node("div", "cabin-front");
    front.append(node("span", "", train ? "Coach A · Front" : "Front of bus"));
    if (!train) front.append(node("span", "steering-wheel"));
    const grid = node("div", `seat-grid${train ? " train-seats" : ""}`);
    grid.setAttribute("role", "group");
    grid.setAttribute("aria-label", `${journey.service.mode} seat selection`);
    const eachSide = train ? 3 : 2;
    journey.seats.forEach((seat, index) => {
      if (index % (eachSide * 2) === eachSide) grid.append(node("span", "seat-aisle"));
      const booked = seat.status === "Booked";
      const selected = seat.number === state.preferredSeat;
      const button = action(String(seat.number), `seat${booked ? " booked" : ""}${selected ? " selected" : ""}`, () => {
        state.preferredSeat = seat.number;
        renderSeats();
        $("seat-map").querySelector(`[data-seat="${seat.number}"]`)?.focus({ preventScroll: true });
      });
      button.dataset.seat = seat.number;
      button.disabled = booked;
      button.setAttribute("aria-label", `Seat ${seat.number}, ${booked ? "booked" : selected ? "selected" : "available"}`);
      button.setAttribute("aria-pressed", String(selected));
      grid.append(button);
    });
    cabin.append(front, grid);
    $("seat-map").replaceChildren(cabin);
    const automatic = state.preferredSeat === null;
    $("auto-seat").classList.toggle("active", automatic);
    $("auto-seat").setAttribute("aria-pressed", String(automatic));
    $("seat-status").textContent = !journey.counts.available
      ? `This service is full. You’ll join the waiting queue after ${journey.counts.waiting} ${journey.counts.waiting === 1 ? "traveller" : "travellers"}. A seat is assigned when a confirmed reservation is cancelled.`
      : automatic ? `${journey.counts.available} seats available. We’ll assign the lowest free seat when you reserve.`
        : `Seat ${state.preferredSeat} selected. It will be checked again when you reserve.`;
  }

  function readPassenger() {
    return {
      name: $("passenger-name").value.trim(),
      contact: $("passenger-contact").value.trim(),
      age: Number($("passenger-age").value),
      gender: $("passenger-gender").value
    };
  }

  async function reviewReservation(event) {
    event.preventDefault();
    if (state.busy || !state.journey) return;
    clearErrors("passenger-form", "passenger-error");
    const passenger = readPassenger();
    const fields = {};
    if (!passenger.name) fields["passenger-name"] = "Enter the traveller’s full name.";
    if (!/^[+\d\s()-]+$/.test(passenger.contact) || !/^\d{10,15}$/.test(passenger.contact.replace(/[+\s()-]/g, ""))) fields["passenger-contact"] = "Enter a valid contact number with 10–15 digits.";
    if (!$("passenger-age").value || !Number.isInteger(passenger.age) || passenger.age < 1 || passenger.age > 120) fields["passenger-age"] = "Enter a whole age from 1 to 120.";
    if (!passenger.gender) fields["passenger-gender"] = "Select a gender option.";
    if (validationError("passenger-error", fields)) return;
    state.passenger = passenger;
    formBusy($("passenger-form"), true);
    state.journey = null;
    $("seat-map").replaceChildren();
    $("seat-loading").hidden = false;
    try {
      const journey = await refreshJourney();
      if (state.preferredSeat !== null && journey.seats.find((seat) => seat.number === state.preferredSeat)?.status !== "Available") {
        state.preferredSeat = null;
        renderSeats();
        throw new ApiError("That seat has just been booked. Please choose another seat or use automatic allocation.");
      }
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
    const passenger = state.passenger;
    const sheet = node("div", "review-sheet");
    const traveller = node("section");
    traveller.append(node("h2", "", "The traveller"));
    const details = node("dl");
    details.append(dataCell("Full name", passenger.name), dataCell("Contact number", passenger.contact),
      dataCell("Age", `${passenger.age} years`), dataCell("Gender", passenger.gender));
    traveller.append(details);
    const side = node("section", "review-side");
    side.append(node("h2", "", "The journey"));
    const service = node("div", "review-service");
    service.append(node("h3", "", `${journey.service.source} to ${journey.service.destination}`),
      node("p", "", `${journey.service.name} · ${journey.service.number} · ${journey.service.mode}`),
      node("p", "", `${dateLabel(journey.date, true)} · ${journey.service.departure} – ${journey.service.arrival}${journey.service.arrival_day_offset ? " (+1 day)" : ""}`));
    if (journey.demo) service.append(node("span", "tag", "Fictional demonstration"));
    const expected = journey.counts.available ? "Confirmed" : "Waiting";
    const seat = state.preferredSeat === null ? (journey.counts.available ? "Automatic · lowest available" : `Waiting queue · position ${journey.counts.waiting + 1}`) : `Seat ${state.preferredSeat}`;
    const allocation = node("dl");
    const status = dataCell("Expected status", "");
    status.querySelector("dd").append(statusBadge(expected));
    allocation.append(dataCell("Seat preference", seat), status);
    const total = node("div", "fare-total");
    total.append(node("span", "", "Total fare"), node("strong", "", money(journey.service.fare)));
    side.append(service, allocation, total, node("p", "muted small", "No payment is collected. Your final seat or queue position appears on the ticket."));
    sheet.append(traveller, side);
    $("review-content").replaceChildren(sheet);
    notice("review-error", "");
  }

  const passengerFieldMap = {
    name: "passenger-name", age: "passenger-age", gender: "passenger-gender", contact: "passenger-contact",
    "passenger.name": "passenger-name", "passenger.age": "passenger-age", "passenger.gender": "passenger-gender", "passenger.contact": "passenger-contact"
  };

  async function confirmBooking() {
    if (state.busy || !state.passenger || !state.journey) return;
    notice("review-error", "");
    const button = $("confirm-booking");
    const oldText = button.textContent;
    button.textContent = "Reserving your place…";
    try {
      const result = await mutate("/api/book", { ...state.selected, passenger: state.passenger, seat_number: state.preferredSeat });
      state.journey = result.journey;
      state.ticket = result.ticket;
      renderTicket(result.ticket);
      show("ticket", result.ticket.demo ? "demo" : "home");
      resetSearchResults();
    } catch (error) {
      if (Object.keys(error.fields || {}).some((field) => passengerFieldMap[field])) {
        show("details", state.selected.demo ? "demo" : "home");
        fail("passenger-error", error, passengerFieldMap);
      } else {
        fail("review-error", error);
        if (error.status === 409) {
          const edit = action("Update seats and edit reservation", "text-button", async () => {
            show("details", state.selected.demo ? "demo" : "home");
            state.journey = null;
            $("seat-map").replaceChildren();
            $("continue-review").disabled = true;
            try {
              const journey = await refreshJourney();
              if (state.preferredSeat !== null && journey.seats.find((seat) => seat.number === state.preferredSeat)?.status !== "Available") state.preferredSeat = null;
              renderSeats();
              $("continue-review").disabled = false;
            } catch (refreshError) { fail("passenger-error", refreshError); }
          });
          $("review-error").append(node("br"), edit);
        }
      }
    } finally { button.replaceChildren(document.createTextNode(oldText), icon("arrow")); }
  }

  function renderTicket(ticket) {
    state.ticket = ticket;
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
  $("auto-seat").addEventListener("click", () => { state.preferredSeat = null; renderSeats(); });
  $("passenger-form").addEventListener("submit", reviewReservation);
  $("back-details").addEventListener("click", () => { show("details", state.selected.demo ? "demo" : "home"); renderSeats(); });
  $("confirm-booking").addEventListener("click", confirmBooking);
  $("print-ticket").addEventListener("click", () => window.print());
  $("cancel-ticket").addEventListener("click", cancelTicket);
  $("ticket-new").addEventListener("click", () => {
    $("passenger-form").reset();
    state.passenger = null;
    state.preferredSeat = null;
    navigate("home");
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
  initialize();
})();
