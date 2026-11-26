-- Filtro do pandoc usado por montar.py ao gerar os HTML do pacote (LEIA-ME.html e
-- TUTORIAL.html).
--
-- Links relativos para outros .md da pasta pd-gaiaball (README.md, PUBLICAR-NO-DEKEN.md...),
-- que não vão no pacote, viram links para o GitHub, na versão `ref` (metadado). Links para
-- os exemplos (exemplos/...) e para o próprio TUTORIAL.html continuam relativos: esses
-- arquivos vão no pacote.

local ref = 'main'

local function ler_meta(m)
  if m.ref then ref = pandoc.utils.stringify(m.ref) end
end

local REPO = 'https://github.com/GaiaSenses/gaiaball/blob/'

local function link(el)
  local alvo = el.target
  if alvo:match('^%a[%w+.-]*:') or alvo:match('^#') then return nil end
  local arquivo, ancora = alvo:match('^([^#]+%.md)(#?.*)$')
  if arquivo and not arquivo:match('^%.%./') then
    el.target = REPO .. ref .. '/pd-gaiaball/' .. arquivo .. ancora
    return el
  end
  return nil
end

return {
  { Meta = ler_meta },
  { Link = link },
}
