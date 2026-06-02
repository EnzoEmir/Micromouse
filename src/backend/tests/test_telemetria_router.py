import pytest
from fastapi.testclient import TestClient
from sqlmodel import Session, select
from app.models.corrida import Corrida

def test_fluxo_telemetria_completo_sucesso(client: TestClient, session: Session):
    resp_init = client.post("/api/telemetria/pacote", json={"tipo": 0, "timestamp_ms": 100, "dimensao": 16, "bateria": 100})
    assert resp_init.status_code == 201
    resp_mov = client.post("/api/telemetria/pacote", json={"tipo": 1, "timestamp_ms": 2000, "x": 1, "y": 2, "w": 5})
    assert resp_mov.status_code == 201
    resp_end = client.post("/api/telemetria/pacote", json={"tipo": 3, "timestamp_ms": 5000, "sucesso": True, "v_med": 20.0, "bateria": 95})
    assert resp_end.status_code == 201
    id_corrida_banco = resp_init.json()["estado"]["id_corrida_banco"]
    corrida = session.get(Corrida, id_corrida_banco)
    assert corrida is not None and corrida.bateria_inicial == 100 and corrida.desafio_cumprido is True

@pytest.mark.parametrize("nome_caso, payload, erro_esperado", [
    ("Falta campo tipo", {"timestamp_ms": 100, "dimensao": 4, "bateria": 90}, "Tipo de pacote não reconhecido."),
    ("Tipo desconhecido (99)", {"tipo": 99, "timestamp_ms": 100, "dimensao": 4, "bateria": 90}, "Tipo de pacote não reconhecido."),
    ("Timestamp negativo", {"tipo": 0, "timestamp_ms": -10, "dimensao": 4, "bateria": 90}, "Campo 'timestamp_ms' não pode ser negativo (recebido: -10)."),
    ("Dimensão inválida (5)", {"tipo": 0, "timestamp_ms": 0, "dimensao": 5, "bateria": 90}, "Dimensão inválida: deve ser 4, 8 ou 16 (recebido: 5)."),
    ("Bateria fora do limite (110)", {"tipo": 0, "timestamp_ms": 0, "dimensao": 4, "bateria": 110}, "Bateria fora do range [0, 100] (recebido: 110)."),
    ("Sucesso não booleano", {"tipo": 3, "timestamp_ms": 1000, "sucesso": "ok", "v_med": 10, "bateria": 80}, "Campo 'sucesso' deve ser booleano (recebido: ok)."),
    ("v_med negativo", {"tipo": 3, "timestamp_ms": 1000, "sucesso": True, "v_med": -1, "bateria": 80}, "Campo 'v_med' não pode ser negativo (recebido: -1)."),
    ("w fora do range (16)", {"tipo": 1, "timestamp_ms": 1000, "x": 0, "y": 0, "w": 16}, "Campo 'w' fora do range [0, 15] (recebido: 16)."),
])
def test_falhas_validacao_barreira(client: TestClient, session: Session, nome_caso, payload, erro_esperado):
    response = client.post("/api/telemetria/pacote", json=payload)
    assert response.status_code == 422
    data = response.json()
    assert data["detail"]["mensagem"] == "Pacote descartado"
    assert any(erro_esperado in e for e in data["detail"]["erros"])

def test_falha_timestamp_regressivo_isolado(client: TestClient):
    client.post("/api/telemetria/pacote", json={"tipo": 0, "timestamp_ms": 5000, "dimensao": 4, "bateria": 90})
    response = client.post("/api/telemetria/pacote", json={"tipo": 1, "timestamp_ms": 4000, "x": 1, "y": 1, "w": 0})
    assert response.status_code == 422
    assert "Timestamp regressivo: 4000 < último válido 5000." in response.json()["detail"]["erros"]

def test_pacote_nao_inicial_sem_sessao_ativa(client: TestClient):
    response = client.post("/api/telemetria/pacote", json={"tipo": 1, "timestamp_ms": 1000, "x": 0, "y": 0, "w": 0})
    assert response.status_code == 409
