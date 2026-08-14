@echo off
rem Avvia l'emulatore.
rem
rem %~dp0 e' la cartella di QUESTO file, con la barra finale: cosi' funziona
rem ovunque si scompatti l'archivio. E' anche il motivo per cui qui non c'e' un
rem collegamento .lnk -- un .lnk incorpora il percorso assoluto della macchina
rem che lo ha creato, quindi confezionato altrove sarebbe rotto per tutti.
rem
rem Le virgolette vuote dopo start non sono un errore: start interpreta il primo
rem argomento citato come TITOLO della finestra, e senza di esse prenderebbe per
rem titolo il percorso dell'eseguibile e non lo avvierebbe.
start "" "%~dp0runtime\bin\Habumi.exe"
