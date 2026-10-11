# Film stasera

Un tile con i film in TV stasera in Italia: l'orario d'inizio, il titolo e, sotto, il canale e il genere. Appartiene
a **un sensore di Home Assistant** e non scarica niente da internet.

## Come si usa

1. In Home Assistant crea il sensore `sensor.film_stasera`: lo stato e' il numero di film, con quattro attributi
   della stessa lunghezza, una voce per film: `titles`, `channels`, `starts` (secondi dal 1970) e `genres`.
   L'attributo facoltativo `stale` (true o false) dice che la guida e' vecchia.
2. Aggiungi il plugin a uno schermo in Tessera. Lo schermo si ricompila una volta.
3. In Layout metti il tile **Film stasera** dalla libreria (gruppo Plugin), da 2x2 a 4x4: mostra tutti i film che
   ci stanno. Poi scegli il sensore nell'ispettore.

## Da sapere

- Su un tile largo (circa 13 cm o piu', come quattro colonne di uno schermo da 7 pollici) i film vanno su due colonne,
  uno dopo l'altro, cosi' una serata intera sta in una pagina.
- Un film iniziato meno di due ore e mezza fa resta nel tile, con l'orario nel colore d'accento: la guida non ha gli
  orari di fine. Il tile segue l'orologio dello schermo, quindi un film esce dalla lista da solo.
- Quando non resta nessun film lo dice, e quando la guida e' vecchia lo scrive accanto alla data.
- Non servono chiavi ne' account.
