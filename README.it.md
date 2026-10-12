# Film stasera

Un tile con i film in TV stasera in Italia: l'orario d'inizio, il titolo e, sotto, il canale e il genere. Appartiene
a **un sensore di Home Assistant** e non scarica niente da internet.

## Come si usa

1. In Home Assistant crea il sensore `sensor.film_stasera`: lo stato e' il numero di film, con quattro attributi
   della stessa lunghezza, una voce per film: `titles`, `channels`, `starts` (secondi dal 1970) e `genres`. Facoltativi: `groups` (un numero da 1 a 4 per film, il colore del pallino
   davanti al nome del canale: 1 blu, 2 ambra, 3 verde, 4 viola; 0 o assente: nessun pallino) e `stale` (true o
   false: la guida e' vecchia).
2. Aggiungi il plugin a uno schermo in Tessera. Lo schermo si ricompila una volta.
3. In Layout metti il tile **Film stasera** dalla libreria (gruppo Plugin), da 1x1 a 4x4: mostra tutti i film che
   ci stanno. Poi scegli il sensore nell'ispettore.

## Da sapere

- Su un tile largo (circa 13 cm o piu', come quattro colonne di uno schermo da 7 pollici) i film vanno su due colonne,
  uno dopo l'altro, cosi' una serata intera sta in una pagina.
- Un film iniziato meno di due ore e mezza fa resta nel tile, con l'orario nel colore d'accento: la guida non ha gli
  orari di fine. Il tile segue l'orologio dello schermo, quindi un film esce dalla lista da solo.
- Quando non resta nessun film lo dice, e quando la guida e' vecchia lo scrive accanto alla data.
- Non servono chiavi ne' account.
- Provato su un vero Waveshare ESP32-S3-Touch-LCD-7B (7 pollici, firmware Tessera 0.55.0). Nel banco di prova dei
  plugin (`tools/preview.py`) si disegna senza avvisi anche sul Waveshare da 5 pollici e sul CYD (2,8 pollici).

## Modalità e pulsante Aggiorna

Il tile ha due modalità. **Film** (quella di partenza) elenca i film di stasera. **Programmi** elenca, per ogni canale in
ordine di numero del telecomando, il programma di prima serata e quello di seconda serata, ognuno con il suo orario;
quando i canali non entrano, la pagina cambia da sola ogni 8 secondi. Un tocco sul tile cambia modalità; l'opzione
"Modalità iniziale" dice da quale parte.

Quando mancano i dati (il sensore non ha un valore, o la sua guida non è di oggi) il tile lo dice e mostra un pulsante
**Aggiorna**. Un tocco chiede a Home Assistant di aggiornare il sensore, cioè di lanciare lo script che scarica la guida.
Lo schermo deve poter eseguire azioni di Home Assistant.

Per i Programmi il sensore ha bisogno degli attributi `prog_channels`, `prog_groups`, `prog1_titles`, `prog1_starts`,
`prog2_titles` e `prog2_starts`: arrivano con la versione 0.3 dello script `film-stasera-ha`. L'app manda a ogni tile al
massimo 2600 byte: in una serata con molti film le liste vengono tagliate alla stessa lunghezza.
