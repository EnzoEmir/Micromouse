"""Add evento table for critical alerts

Revision ID: a9f8e6d4c2b1
Revises: 74d69c648eb5
Create Date: 2026-05-18 00:00:00.000000

"""
from typing import Sequence, Union

from alembic import op
import sqlalchemy as sa


# revision identifiers, used by Alembic.
revision: str = 'a9f8e6d4c2b1'
down_revision: Union[str, Sequence[str], None] = '74d69c648eb5'
branch_labels: Union[str, Sequence[str], None] = None
depends_on: Union[str, Sequence[str], None] = None


def upgrade() -> None:
    """Upgrade schema."""
    op.create_table(
        'evento',
        sa.Column('id_evento', sa.Integer(), sa.Identity(always=False), nullable=False),
        sa.Column('tipo_evento', sa.String(length=100), nullable=False),
        sa.Column('descricao', sa.Text(), nullable=False),
        sa.Column('timestamp_ms', sa.Integer(), nullable=False),
        sa.Column('id_corrida', sa.Integer(), nullable=True),
        sa.ForeignKeyConstraint(['id_corrida'], ['corrida.id_corrida'], ondelete='SET NULL'),
        sa.PrimaryKeyConstraint('id_evento', name='pk_evento_id_evento'),
        if_not_exists=True,
    )


def downgrade() -> None:
    """Downgrade schema."""
    op.drop_table('evento')
