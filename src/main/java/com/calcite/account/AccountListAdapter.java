package com.calcite.account;

import android.content.Context;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.BaseAdapter;
import android.widget.RadioButton;
import android.widget.TextView;

import com.calcite.R;

import java.util.List;

public class AccountListAdapter extends BaseAdapter {

    private final Context context;
    private final List<Account> accounts;
    private int selectedPosition = 0;
    private OnAccountDeleteListener deleteListener;
    private OnAccountSelectedListener selectedListener;

    public interface OnAccountDeleteListener {
        void onDelete(int position);
    }

    public interface OnAccountSelectedListener {
        void onSelected(int position, Account account);
    }

    public void setOnAccountDeleteListener(OnAccountDeleteListener listener) {
        this.deleteListener = listener;
    }

    public void setOnAccountSelectedListener(OnAccountSelectedListener listener) {
        this.selectedListener = listener;
    }

    public AccountListAdapter(Context context, List<Account> accounts) {
        this.context = context;
        this.accounts = accounts;
    }

    public void setSelectedPosition(int position) {
        if (position >= 0 && position < accounts.size()) {
            selectedPosition = position;
        } else if (accounts.size() > 0) {
            selectedPosition = 0;
        }
        notifyDataSetChanged();
    }

    public int getSelectedPosition() {
        return selectedPosition;
    }

    public Account getSelectedAccount() {
        if (accounts.isEmpty()) return null;
        return accounts.get(selectedPosition);
    }

    @Override
    public int getCount() {
        return accounts.size();
    }

    @Override
    public Object getItem(int position) {
        return accounts.get(position);
    }

    @Override
    public long getItemId(int position) {
        return position;
    }

    @Override
    public View getView(int position, View convertView, ViewGroup parent) {
        if (convertView == null) {
            convertView = LayoutInflater.from(context).inflate(R.layout.item_account, parent, false);
        }

        Account account = accounts.get(position);

        RadioButton radio = convertView.findViewById(R.id.radioAccount);
        TextView tvName = convertView.findViewById(R.id.tvAccountName);
        TextView tvType = convertView.findViewById(R.id.tvAccountType);
        TextView btnDelete = convertView.findViewById(R.id.btnDeleteAccount);

        radio.setChecked(position == selectedPosition);
        tvName.setText(account.getName());
        tvType.setText(account.getTypeDisplay());

        radio.setOnClickListener(v -> {
            selectedPosition = position;
            notifyDataSetChanged();
            if (selectedListener != null) selectedListener.onSelected(position, account);
        });

        convertView.setOnClickListener(v -> {
            selectedPosition = position;
            notifyDataSetChanged();
            if (selectedListener != null) selectedListener.onSelected(position, account);
        });

        btnDelete.setOnClickListener(v -> {
            if (deleteListener != null) {
                deleteListener.onDelete(position);
            }
        });

        return convertView;
    }
}
