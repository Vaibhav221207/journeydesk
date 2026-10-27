# Railway / Bus Reservation System — Journey Desk

## Project definition

Journey Desk manages passenger reservations, seat availability, cancellations, passenger search and waiting lists for a small catalogue of fictional direct bus and train services. Its objective is to demonstrate arrays, linked lists, queues and priority queues in a complete application that a student can run and explain.

**Backend:** C++20. **Frontend:** HTML, CSS and JavaScript. **Storage:** local JSON file. **Interface:** browser, served by the native C++ process. HTTP and JSON libraries are included; the reservation algorithms and structures are implemented in the project's own source.

## Requirements and implementation

| Required operation | Implementation |
|---|---|
| Display available seats | Journey API returns numbered seat availability; browser draws a bus or train layout |
| Book a seat | Select preferred available seat or pop the lowest seat from the min-heap |
| Cancel a ticket | Validate booking reference/contact, preserve cancelled history, release or reassign its seat |
| Assign cancelled seats to waiting passengers | Pop FIFO head and transfer the exact cancelled seat |
| Search passenger records | Desk filters records by case-insensitive name, passenger ID or booking reference |
| Display passenger list | Separate desk manifest, ordered by confirmed seat |
| Maintain a waiting queue | Full journeys enqueue new reservations; queue positions update after cancellation/promotion |

The revised interface also supports route/date search, sample service times and fares, group booking for one to six travellers, combined review, individual ticket lookup/cancellation, and printing all tickets together. Regular journeys begin empty. Loading fictional sample records is an explicit demo action.

## Input and output

Passenger input: full name, age, gender and contact number. The backend generates a passenger ID and a random booking reference, which prevents users having to invent unique IDs. Journey input: transport mode, source, destination, date, chosen service and an optional preferred seat.

Groups enter one shared contact number and a name, age, gender and optional seat preference for each traveller. Output includes service details, total sample fare and each person's confirmed seat or waiting position, passenger ID and booking reference. Ticket lookup displays the current status, including cancellation. The reservation desk displays the assignment's manifest and waiting list.

## Data structures

### Seat array

Each journey contains `std::array<std::string, 18>`. Bus services use the first 12 entries; trains use 18. An empty entry denotes a free seat; an occupied entry holds a booking reference. Mapping seat number to array index is `seat_number - 1`.

### Singly linked list

`LinkedList<Booking>` stores passenger booking records. Each node owns its successor. A record contains generated identity, name, age, gender, normalized contact, booking status and seat. Cancelled records remain for ticket history, while the active manifest omits them. Lookup traverses the list and applies a predicate. Deep copies allocate independent nodes so a candidate change cannot mutate live records before a successful save.

### FIFO queue

`LinkedQueue<std::string>` stores waiting booking references. It maintains head and tail pointers. New waiting passengers join at the tail; promotion removes the head. Removing a cancelled waiting reservation preserves everyone else's order. The queue is independent for each service, date and demo namespace.

### Priority queue

`MinHeap` stores free seat numbers in a custom binary minimum heap. Automatic allocation removes the smallest available seat. If cancellation leaves no waiting passengers, the released seat is inserted into the heap. Preferred-seat booking searches for that seat, removes it and repairs heap ordering. This use of priority affects **seat number**, while passengers continue to be served in FIFO order.

## Main algorithms

Booking:

1. Validate the journey, date and passenger fields.
2. Reject a duplicate active passenger with the same normalized name/contact on the same journey.
3. Copy the current state into an independent candidate.
4. Allocate the preferred seat or lowest free seat; if full, append the booking reference to the waiting queue.
5. Append the passenger record to the linked list.
6. Save the candidate atomically, then publish it as live state.
7. Return the ticket and current public seat map.

Group booking:

1. Validate one to six passenger entries and the shared contact number. Reject duplicate active names/contact pairs, repeated seat preferences and already occupied preferred seats.
2. Copy the live state once. Remove every selected preferred seat from the candidate min-heap before allocating automatic seats, so automatic allocation cannot take another group member's choice.
3. In passenger order, assign each reserved preference or pop the lowest remaining free seat. If no seat remains, append that passenger's reference to the FIFO queue.
4. Append an ordinary linked-list record with a separate passenger ID and ticket reference for each traveller.
5. Save the entire candidate once, then publish it. Any validation, conflict or write error leaves all original records, seats and ID counters unchanged.
6. Return the combined receipt and total sample fare. Each ticket can subsequently be looked up or cancelled independently with the shared contact.

Cancellation:

1. Find the reservation and validate reference/contact, or authorized desk access within the selected journey.
2. If waiting, remove its reference from the queue and mark it cancelled.
3. If confirmed and the queue is nonempty, remove the queue head and give it the exact cancelled seat.
4. If confirmed and nobody is waiting, clear the array slot and return its number to the heap.
5. Save before publishing, preserve cancelled history, and return the updated result.

Search and route discovery use 25 fictional direct services among Ahmedabad, Mumbai, Surat and Pune. Each city connects to the other three by both bus and train, giving 24 directed route-and-transport combinations; Ahmedabad–Mumbai has a second bus departure. Matching uses the selected source/destination/mode, and service results are ordered by departure time. No intermediate-station seat sharing is implemented.

## Example demonstration

Use the explicit **Demo** view and load fictional records. The bus GJ01 has 12 confirmed passengers, including P102 Priya Patel in seat 2. Its waiting queue is:

1. P108 — Neha Patel
2. P109 — Raj Kumar
3. P110 — Amit Patel

Cancel Priya Patel's reservation. The system assigns **seat 2 to P108 Neha Patel**. Raj becomes position 1 and Amit position 2. The number of booked seats stays 12; waiting count decreases from 3 to 2. Resetting the sample restores this example without changing regular bookings.

## Complexity

Let `P` be booking records in a journey, `W` its waiting length, `S` seat capacity and `T` all stored records.

| Operation | Structure-level cost |
|---|---|
| Inspect a seat array entry | O(1) |
| Display all seats | O(S) |
| Append to linked list | O(1) with tail |
| Search passenger linked list | O(P) |
| Queue enqueue/dequeue | O(1) |
| Remove an arbitrary waiting passenger | O(W) |
| Find queue position | O(W) |
| Heap insert or minimum removal | O(log S) |
| Preferred seat removal | O(S) search, then O(log S) repair |

Complete application operations include validation, record lookup, candidate deep copying and file serialization. They are **not O(1)** just because one underlying queue or array operation is O(1). Mutations copy and persist stored state, so the total work grows with `T`; this deliberate tradeoff makes atomic rollback straightforward for a small college project. Heap/queue costs above describe the individual structures.

## Validation and consistency

- Age must be a whole number from 1 to 120; gender must match a displayed option.
- Contact normalizes supported punctuation to 10–15 digits. Name is trimmed and limited to 100 characters.
- Normal booking dates must be valid calendar dates between today and 365 days ahead. Existing tickets remain readable/cancellable when their date passes.
- Occupied preferred seats and duplicate active reservations cause a conflict, with no partial change.
- A mutex serializes operations in one server process. Candidate copies ensure failed writes leave live state unchanged.
- Atomic file replacement publishes complete snapshots. Corrupt storage is rejected and preserved for recovery.
- Public availability responses contain no passenger identities. Individual ticket lookup uses reference/contact; the manifest requires a desk key, except for clearly labelled fictional demo records.
- The desk key is configured through `RESERVATION_DESK_KEY` or generated once into a private `desk.key` file beside the local booking data. Restarting reuses the key. Key files are excluded from version control, Docker build context and submission archives.

## Viva questions

**Why use four structures?** Each solves a different problem: indexed seat lookup, dynamic passenger records, fair arrival-order waiting, and smallest-number free-seat selection.

**Why does a cancelled seat go straight to the queue head?** The released seat is already known; assigning it directly preserves FIFO priority and avoids making that seat temporarily available to a newer passenger.

**Why a min-heap?** It repeatedly provides the lowest available seat without sorting all free seats for each automatic booking.

**Why not use only standard containers?** Custom list, queue and heap expose the node links and heap algorithms for the assignment. Standard facilities are used for supporting tasks such as service lookup, dates and file handling.

**What happens when a waiting passenger cancels?** Remove that queue node, mark the record cancelled and update subsequent positions. No booked seat becomes free.

**Does frontend JavaScript allocate seats?** No. It submits a preference; the C++ engine checks availability and decides the result. The UI redraws the returned server state.

**What prevents two simultaneous requests taking one seat?** The engine mutex protects the complete validation/candidate/save/publication transaction.

**What happens if saving fails?** The candidate is discarded; no new live reservation or seat change is published.

**Is this connected to IRCTC?** No. The step-by-step journey search and review flow is inspired by published booking guidance, while the timetable and data belong to this classroom project.

## Limits

This is a single-process educational application with up to six travellers per submission, individual tickets, fixed capacities and a small direct-service catalogue. It has no payment/refund processing, multi-server database, intermediate route occupancy, SMS, account login, railway quotas or real transport integration. Native hosting requirements and filesystem persistence are described in `HOSTING.md`.
