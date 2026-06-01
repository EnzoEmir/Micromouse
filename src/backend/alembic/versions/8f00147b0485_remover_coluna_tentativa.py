"""remover_coluna_tentativa

Revision ID: 8f00147b0485
Revises: 7e99036a0374
Create Date: 2026-06-01 19:10:00.000000

"""
from typing import Sequence, Union

from alembic import op
import sqlalchemy as sa


revision: str = '8f00147b0485'
down_revision: Union[str, Sequence[str], None] = '7e99036a0374'
branch_labels: Union[str, Sequence[str], None] = None
depends_on: Union[str, Sequence[str], None] = None


def upgrade() -> None:
    op.drop_column('corrida', 'tentativa')


def downgrade() -> None:
    op.add_column('corrida', sa.Column('tentativa', sa.Integer(), nullable=True))
