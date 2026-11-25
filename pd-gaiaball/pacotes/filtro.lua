-- Filtro do pandoc usado por montar.sh ao gerar os HTML dos pacotes.
--
-- 1. Trechos de um sistema só: um bloco ::: {.so-windows} ... ::: ou um trecho
--    [texto]{.so-macos} fica só no HTML daquele sistema (metadado `so`). Uma classe
--    pode listar vários: {.so-windows .so-linux}.
-- 2. Links relativos para outros .md do repositório (README.md, PUBLICAR-NO-DEKEN.md...),
--    que não vão no pacote, viram links para o GitHub, na versão `ref` (metadado).

local so, ref

local function ler_meta(m)
  so = m.so and pandoc.utils.stringify(m.so) or nil
  ref = m.ref and pandoc.utils.stringify(m.ref) or 'main'
end

-- nil = o elemento não é de um sistema só; true/false = é, e (não) é deste
local function deste_sistema(el)
  local marcado = false
  for _, c in ipairs(el.classes) do
    if c:match('^so%-') then
      marcado = true
      if c == 'so-' .. so then return true end
    end
  end
  if marcado then return false end
  return nil
end

local function filtrar(el)
  if not so then return nil end
  local d = deste_sistema(el)
  if d == nil then return nil end
  if d then return el.content end -- tira o invólucro, fica o conteúdo
  return {}
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
  { Div = filtrar, Span = filtrar, Link = link },
}
