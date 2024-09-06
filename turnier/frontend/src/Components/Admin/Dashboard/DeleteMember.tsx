import { Button, Dialog, DialogActions, DialogContent, DialogContentText, DialogTitle, Stack } from '@mui/material'
import React from 'react'
import style from './admin.module.scss'
import { doRequest } from '../../Common/StaticFunctionsTyped'
import { useDispatch } from 'react-redux'

type Props = {
    isOpen: boolean,
    close: () => void,
    memberName: string | null,
    memberAlias: string
}

const DeleteMember = (props: Props) => {
    const dispatch = useDispatch()
    return (
        <Dialog open={props.isOpen} onClose={props.close} sx={{ zIndex: 20000000 }} >
            <DialogTitle>{"Verein Löschen"}</DialogTitle>
            <DialogContent >
                <DialogContentText>
                    Bist du dir sicher, dass du {props.memberAlias} löschen möchtest?
                </DialogContentText>


            </DialogContent>
            <DialogActions>
                <Stack direction="row" justifyContent="space-between" className={style.buttonContainer}>
                    <Button onClick={props.close} variant='outlined'>Abbrechen</Button>
                    <Button onClick={() => {
                        doRequest("DELETE", "members", { name: props.memberName }, dispatch).then((value) => {
                            if (value.code === 200) {
                                props.close()
                            }
                        })
                    }} variant='contained' color='error'>
                        Löschen
                    </Button>
                </Stack>
            </DialogActions>
        </Dialog >
    )
}

export default DeleteMember