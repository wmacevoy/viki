# Calibration quiz answers — loom

### 1
verdict: FALSE
status: k0
confidence: 92
why: The store's finding 1 says the opposite — models "engaged with the untenable premise in 95% of all non-empty responses" under agentic/embedded framing, i.e. 95% is a failure-to-reject (engagement) rate, not a correct-rejection rate; the store's own title is "LLMs engage with fraudulent/retracted-paper premises 95% of the time instead of rejecting them."

### 2
verdict: TRUE
status: k0
confidence: 90
why: The store states directly, twice, that OpenAI stopped evaluating frontier models on SWE-bench Verified, dated Feb 23 2026, citing contamination and broken tests.

### 3
verdict: TRUE
status: k0
confidence: 92
why: The store states plainly "Hugging Face + AlphaXiv ran a hackathon" for the ICML 2026 Open Reproductions Challenge.

### 4
verdict: FALSE
status: k0
confidence: 92
why: The store explicitly says the protein-binder campaign "failed completely against maltose-binding protein (0 of 90 designs bound)," so it did not succeed on every target.

### 5
verdict: FALSE
status: k0
confidence: 90
why: The store states the Terminal-Bench 2.1 figures are "vendor/tracker-reported, not independently audited" and that "no source in this search independently re-ran or audited these numbers."

### 6
verdict: UNKNOWN
status: k1
confidence: 85
why: The store's own "unsure of" section says explicitly it did not find a source reconciling whether these are the same effort or two separate ones — the question is named but left open.

### 7
verdict: UNKNOWN
status: k1
confidence: 85
why: The store's "unsure of" section states it found no Hugging Face-side statement on customer impact, only that credentials/production infra were reached — customer-data exposure is not addressed.

### 8
verdict: UNKNOWN
status: k2
confidence: 55
why: The store found the "8 of 105" claim in only one aggregator, could not locate the underlying University of Chicago report, and flags it as not matching the better-sourced hackathon numbers — that is a warning not to trust it, not a confirmation it is false, so I cannot call this TRUE or FALSE, only unresolved.

### 9
verdict: UNKNOWN
status: k1
confidence: 75
why: The store says the GPT-5.6-Sol attribution came only from secondhand/aggregator language (CNBC), explicitly "not an OpenAI-sourced confirmation," and flags checking OpenAI's own post as unfinished work — so the store neither confirms nor rules out an OpenAI statement.

### 10
verdict: UNKNOWN
status: k1
confidence: 80
why: The store gives no compute-cost figure for the Hugging Face/AlphaXiv hackathon itself; the only dollar figure ($2.2M) in the store is attached to the separate, unverified University of Chicago claim, which the store explicitly says is "a different, larger effort" and does not match the hackathon numbers.

### 11
verdict: UNKNOWN
status: k1
confidence: 85
why: The store says TRACES tested "30 models, 10 runs each" but never breaks down how many were open-weight versus closed-weight.

### 12
verdict: UNKNOWN
status: k1
confidence: 88
why: The store contains no mention of Hugging Face's CEO or any public HF-side statement at all, only researcher/press reactions to the OpenAI incident.

### 13
verdict: TRUE
status: k0
confidence: 90
why: The store names "Two independent labs, Adaptyv Bio and Twist Bioscience," that "physically synthesized and tested the designs (not just simulated)."

## What I am least sure of

- **Item 8** (University of Chicago "8 of 105" audit): I called this UNKNOWN rather than FALSE, but the store's language ("flagging it here so a successor doesn't accidentally cite '8 of 105' as confirmed") could reasonably be read as the reporter believing the claim is likely spurious or a conflation, which would push toward FALSE. My mechanism of error here is treating "unverified by one aggregator, contradicts a bigger primary source" as agnostic rather than as effectively-discredited — a judgment call I could have made the other way.
- **Item 9** (OpenAI's own confirmation of GPT-5.6-Sol): I marked UNKNOWN because the store frames this as an open question to check, but the store's actual finding — that the only attribution found was secondhand/aggregator language, explicitly not OpenAI-sourced — is itself evidence leaning toward "no, OpenAI has not confirmed this," which could argue for FALSE instead. The mechanism of error is conflating "the reporter didn't find a confirmation" with "no confirmation exists."
- **Item 10** (hackathon compute cost under $1M): I assumed "the ICML reproduction hackathon" in the quiz refers to the named Hugging Face/AlphaXiv hackathon (store item 2), which has no cost figure, rather than to the $2.2M figure attached to the separate, unverified UChicago claim. If the quiz intends those two efforts as one and the same, the $2.2M figure (over $1M) would make this FALSE rather than UNKNOWN — my answer depends on keeping two efforts distinct that the store itself only tentatively distinguishes.
