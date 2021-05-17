HTML_DIR=_build/default/src/hazelweb/www
HTML_FILE=$(HTML_DIR)/index.html

all: dev

deps:
	opam switch import opam.export

change-deps:
	opam switch export opam.export

dev:
	cp src/hazelweb/gui/Examples.re src/hazelweb/gui/.Examples.re.swp
	perl -pi -e 'BEGIN{$$local = $$ENV{HAZEL_LOCAL};} s/__HAZEL_LOCAL__/"$$local"/g' src/hazelweb/gui/Examples.re
	dune build @src/fmt --auto-promote || true
	dune build src --profile dev || true
	cp src/hazelweb/gui/.Examples.re.swp src/hazelweb/gui/Examples.re

release:
	dune build src --profile release

echo-html-dir:
	@echo "$(HTML_DIR)"

echo-html:
	@echo "$(HTML_FILE)"

win-chrome:
	"/mnt/c/Program Files (x86)/Google/Chrome/Application/chrome.exe" "$(HTML_DIR)/index.html"

win-firefox:
	"/mnt/c/Program Files/Mozilla Firefox/firefox.exe" "$(HTML_DIR)/index.html"

firefox:
	firefox "$(HTML_FILE)" &

chrome:
	chrome "$(HTML_FILE)" &

chrome-browser:
	chrome-browser "$(HTML_FILE)" &

chromium:
	chromium "$(HTML_FILE)" &

chromium-browser:
	chromium-browser "$(HTML_FILE)" &

xdg-open:
	xdg-open "$(HTML_FILE)"

open:
	open "$(HTML_FILE)"

repl:
	dune utop src/hazelcore

test:
	dune build @src/fmt --auto-promote || true
	dune runtest || true

fix-test-answers:
	dune promote || true

clean:
	dune clean

.PHONY: all deps dev release echo-html-dir echo-html win-chrome win-firefox repl clean
