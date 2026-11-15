# Pocket Pony: getting it onto Google Play

Everything here is done by hand in the Play Console (play.google.com/console), signed in as Blue
Beetle Games. The files mentioned are in this folder, or on the GitHub "android" release.

## Before you start

1. **Put the privacy policy online.** Replace both `CONTACT_EMAIL`s in `privacy.html` with an
   address you check, then upload it to your site so it opens at
   **https://bluebeetle.games/pocket-pony/privacy.html**. The app's Settings > PRIVACY button opens
   exactly that address, so keep it there.
2. **Set up payments**, if you haven't already. A paid app needs a payments profile: Play Console >
   Settings > Payments profile.
3. **Download the app bundle**: `pocket-pony.aab` from the "android" release on GitHub. CI rebuilds
   it on every push to main, signed with your squatch upload key.

## 1. Create the app

Play Console > Create app:
- App name: **Pocket Pony**
- Default language: English (United States)
- App or game: **Game**
- Free or paid: **Paid**
- Tick the declarations and Create.

The package name `games.bluebeetle.pocketpony` comes from the first bundle you upload. It can never
be changed afterwards.

## 2. Store listing

Grow users > Store presence > Main store listing. Copy the text from `listing.md`, and upload:
- App icon: `icon-512.png`
- Feature graphic: `feature-graphic.png`
- Phone screenshots: everything in `screenshots/`, in order

Then fill in Store settings: category **Simulation**, the tags, your contact email and website.

## 3. App content (Policy > App content)

Work through each card:
- **Privacy policy:** https://bluebeetle.games/pocket-pony/privacy.html
- **Ads:** No, the app doesn't contain ads.
- **App access:** All functionality is available without special access.
- **Content rating:** start the IARC questionnaire, category "Game". Answer No to violence, fear,
  sexuality, language, drugs, gambling, user interaction and sharing location. There is one mild
  joke: the pony toots after too many apples. If a question asks about crude or toilet humour,
  answer Yes and choose the mildest level. Expect an Everyone / PEGI 3 or 7 rating.
- **Target audience:** ages **9–12, 13–15, 16–17 and 18 and over**. Choosing 9–12 puts the app
  under the Families policy, which it already meets: no ads, no data collected, a privacy policy
  linked from inside the app. When asked whether it could unintentionally appeal to children,
  you've already said it's for them, so that doesn't apply.
- **Data safety:** "Does your app collect or share any of the required user data types?" **No.**
  This is true: the built app has no internet permission, and its only permission is
  notifications.
- **News app:** No. **Government app:** No. **Financial features:** none. **Health:** none.

## 4. Price

Monetize > Products > App pricing: **$2.99** (Play converts it for other countries). A paid app can
later be made free, but a free app can't become paid.

## 5. The closed test (required for new personal accounts)

Personal accounts created after November 13, 2023 must run a **closed test with at least 12
testers, opted in for 14 days in a row**, before they can publish. Then you apply for production
access, and Google usually answers within a week. Allow about **3 weeks** in all.

1. Testing > Closed testing > Create track (or use "Alpha").
2. Testers: make an email list of **at least 12 people (recruit 15 or more in case some drop
   out)** with Android phones, e.g. family and friends.
3. **Make them license testers too** (Play Console > Settings > License testing, same emails), so
   they can install a paid app without paying.
4. Create a release: upload `pocket-pony.aab`. Accept Play App Signing (Google holds the app's
   signing key; your squatch key stays the upload key). Release notes: "First test of Pocket Pony."
5. Send the testers the opt-in link. Each one must tap **"Become a tester"** and install the app,
   and stay opted in for the full 14 days.
6. After 14 days, go to the Dashboard and choose **Apply for production**. Answer the questions
   about the test honestly (what testers did, what you fixed).
7. Once approved: Production > Create release > the same or a newer bundle > roll out.

## 6. Later updates, automatically (optional)

CI can upload each new build to Play for you:
1. In Google Cloud, create a service account, give it access in Play Console > Users and permissions
   (Release apps to testing tracks), and download its JSON key.
2. In the GitHub repository settings, add the secret **PLAY_SERVICE_ACCOUNT_JSON** (the JSON) and
   the variable **PONY_PLAY_TRACK** = `alpha` (closed testing) or `internal`.
3. Each push to main then sends `pocket-pony.aab` to that track as a draft. Set the variable
   **PONY_RELEASE_STATUS** = `completed` once the first release has gone out by hand.

## What's in the store build

- The horse, quick to trust, born after 5 minutes
- No trail map, and so no Wi-Fi or location permission
- No Galaxy Watch companion and no Google Play services library
- Settings > PRIVACY opens the privacy policy
- No ads, no in-app purchases, no accounts, no internet
