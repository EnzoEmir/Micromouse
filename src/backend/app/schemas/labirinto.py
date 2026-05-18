"""Schemas Pydantic para Labirinto e Célula."""

from pydantic import BaseModel

from ..models.enums import TipoLabirinto


class CelulaResponse(BaseModel):
    """Célula do labirinto com paredes."""

    id_celula: int
    coordenada_x: int
    coordenada_y: int
    parede_norte: bool
    parede_sul: bool
    parede_leste: bool
    parede_oeste: bool


class LabirintoResponse(BaseModel):
    """Labirinto com lista de células."""

    id_labirinto: int
    tipo_labirinto: TipoLabirinto
    celulas: list[CelulaResponse] = []


class LabirintoResumoResponse(BaseModel):
    """Resumo do labirinto sem as células (para listagem)."""

    id_labirinto: int
    tipo_labirinto: TipoLabirinto
